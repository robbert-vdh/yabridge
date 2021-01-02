// yabridge: a Wine VST bridge
// Copyright (C) 2020-2021 Robbert van der Helm
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "vst3.h"

#include "../boost-fix.h"

#include <public.sdk/source/vst/hosting/module_win32.cpp>

#include "vst3-impls/component-handler-proxy.h"
#include "vst3-impls/connection-point-proxy.h"
#include "vst3-impls/host-context-proxy.h"
#include "vst3-impls/plug-frame-proxy.h"

InstanceInterfaces::InstanceInterfaces() {}

InstanceInterfaces::InstanceInterfaces(
    Steinberg::IPtr<Steinberg::FUnknown> object)
    : object(object),
      audio_processor(object),
      component(object),
      connection_point(object),
      edit_controller(object),
      edit_controller_2(object),
      note_expression_controller(object),
      plugin_base(object),
      unit_data(object),
      program_list_data(object),
      unit_info(object) {}

Vst3Bridge::Vst3Bridge(MainContext& main_context,
                       std::string plugin_dll_path,
                       std::string endpoint_base_dir)
    : HostBridge(plugin_dll_path),
      generic_logger(Logger::create_wine_stderr()),
      logger(generic_logger),
      main_context(main_context),
      sockets(main_context.context, endpoint_base_dir, false) {
    std::string error;
    module = VST3::Hosting::Win32Module::create(plugin_dll_path, error);
    if (!module) {
        throw std::runtime_error("Could not load the VST3 module for '" +
                                 plugin_dll_path + "': " + error);
    }

    sockets.connect();

    // Fetch this instance's configuration from the plugin to finish the setup
    // process
    config = sockets.vst_host_callback.send_message(WantsConfiguration{},
                                                    std::nullopt);
}

void Vst3Bridge::run() {
    // XXX: In theory all of thise should be safe assuming the host doesn't do
    //      anything weird. We're using mutexes when inserting and removing
    //      things, but for correctness we should have a multiple-readers
    //      single-writer style lock since concurrent reads and writes can also
    //      be unsafe.
    sockets.host_vst_control.receive_messages(
        std::nullopt,
        overload{
            [&](const Vst3PlugViewProxy::Destruct& request)
                -> Vst3PlugViewProxy::Destruct::Response {
                // XXX: Not sure if his has to be run form the UI thread
                main_context
                    .run_in_context<void>([&]() {
                        // When the pointer gets dropped by the host, we want to
                        // drop it here as well, along with the `IPlugFrame`
                        // proxy object it may have received in
                        // `IPlugView::setFrame()`.
                        object_instances[request.owner_instance_id]
                            .plug_view.reset();
                        object_instances[request.owner_instance_id]
                            .plug_frame_proxy.reset();
                    })
                    .wait();

                return Ack{};
            },
            [&](const Vst3PluginProxy::Construct& request)
                -> Vst3PluginProxy::Construct::Response {
                Steinberg::TUID cid;
                std::copy(request.cid.begin(), request.cid.end(), cid);

                // Even though we're requesting a specific interface (to mimic
                // what the host is doing), we're immediately upcasting it to an
                // `FUnknown` so we can create a perfect proxy object.
                // We create the object from the GUI thread in case it
                // immediatly starts timers or something (even though it
                // shouldn't)
                Steinberg::IPtr<Steinberg::FUnknown> object =
                    main_context
                        .run_in_context<Steinberg::IPtr<Steinberg::FUnknown>>(
                            [&]() -> Steinberg::IPtr<Steinberg::FUnknown> {
                                switch (request.requested_interface) {
                                    case Vst3PluginProxy::Construct::Interface::
                                        IComponent:
                                        return module->getFactory()
                                            .createInstance<
                                                Steinberg::Vst::IComponent>(
                                                cid);
                                        break;
                                    case Vst3PluginProxy::Construct::Interface::
                                        IEditController:
                                        return module->getFactory()
                                            .createInstance<
                                                Steinberg::Vst::
                                                    IEditController>(cid);
                                        break;
                                    default:
                                        // Unreachable
                                        return nullptr;
                                        break;
                                }
                            })
                        .get();

                if (!object) {
                    return UniversalTResult(Steinberg::kResultFalse);
                }

                const size_t instance_id = register_object_instance(object);

                // This is where the magic happens. Here we deduce which
                // interfaces are supported by this object so we can create
                // a one-to-one proxy of it.
                return Vst3PluginProxy::ConstructArgs(
                    object_instances[instance_id].object, instance_id);
            },
            [&](const Vst3PluginProxy::Destruct& request)
                -> Vst3PluginProxy::Destruct::Response {
                unregister_object_instance(request.instance_id);
                return Ack{};
            },
            [&](Vst3PluginProxy::SetState& request)
                -> Vst3PluginProxy::SetState::Response {
                // This same function is defined in both `IComponent` and
                // `IEditController`, so the host is calling one or the other
                if (object_instances[request.instance_id].component) {
                    return object_instances[request.instance_id]
                        .component->setState(&request.state);
                } else {
                    return object_instances[request.instance_id]
                        .edit_controller->setState(&request.state);
                }
            },
            [&](Vst3PluginProxy::GetState& request)
                -> Vst3PluginProxy::GetState::Response {
                VectorStream stream{};
                tresult result;

                // This same function is defined in both `IComponent` and
                // `IEditController`, so the host is calling one or the other
                if (object_instances[request.instance_id].component) {
                    result = object_instances[request.instance_id]
                                 .component->getState(&stream);
                } else {
                    result = object_instances[request.instance_id]
                                 .edit_controller->getState(&stream);
                }

                return Vst3PluginProxy::GetStateResponse{
                    .result = result, .updated_state = std::move(stream)};
            },
            [&](YaConnectionPoint::Connect& request)
                -> YaConnectionPoint::Connect::Response {
                // If the host directly connected the underlying objects then we
                // can directly connect them as well. Otherwise we'll have to go
                // through a connection proxy (to proxy the host's connection
                // proxy).
                return std::visit(
                    overload{
                        [&](const native_size_t& other_instance_id) -> tresult {
                            return object_instances[request.instance_id]
                                .connection_point->connect(
                                    object_instances[other_instance_id]
                                        .connection_point);
                        },
                        [&](Vst3ConnectionPointProxy::ConstructArgs& args)
                            -> tresult {
                            object_instances[request.instance_id]
                                .connection_point_proxy = Steinberg::owned(
                                new Vst3ConnectionPointProxyImpl(
                                    *this, std::move(args)));

                            return object_instances[request.instance_id]
                                .connection_point->connect(
                                    object_instances[request.instance_id]
                                        .connection_point_proxy);
                        }},
                    request.other);
            },
            [&](const YaConnectionPoint::Disconnect& request)
                -> YaConnectionPoint::Disconnect::Response {
                // If the objects were connected directly we can also disconnect
                // them directly. Otherwise we'll disconnect them from our proxy
                // object and then destroy that proxy object.
                if (request.other_instance_id) {
                    return object_instances[request.instance_id]
                        .connection_point->disconnect(
                            object_instances[*request.other_instance_id]
                                .connection_point);
                } else {
                    const tresult result =
                        object_instances[request.instance_id]
                            .connection_point->disconnect(
                                object_instances[*request.other_instance_id]
                                    .connection_point_proxy);
                    object_instances[*request.other_instance_id]
                        .connection_point_proxy.reset();

                    return result;
                }
            },
            [&](const YaConnectionPoint::Notify& request)
                -> YaConnectionPoint::Notify::Response {
                // NOTE: We're using a few tricks here to pass through a pointer
                //       to the _original_ `IMessage` object passed to a
                //       connection proxy. This is needed because some plugins
                //       like iZotope VocalSynth 2 use these messages to
                //       exchange pointers between their objects so they can
                //       break out of VST3's separation, but they might also
                //       store the message object and not the actual pointers.
                //       We should thus be passing a (raw) pointer to the
                //       original object so we can pretend none of this wrapping
                //       and serializing has ever happened.
                return object_instances[request.instance_id]
                    .connection_point->notify(
                        request.message_ptr.get_original());
            },
            [&](YaEditController::SetComponentState& request)
                -> YaEditController::SetComponentState::Response {
                return object_instances[request.instance_id]
                    .edit_controller->setComponentState(&request.state);
            },
            [&](const YaEditController::GetParameterCount& request)
                -> YaEditController::GetParameterCount::Response {
                return object_instances[request.instance_id]
                    .edit_controller->getParameterCount();
            },
            [&](YaEditController::GetParameterInfo& request)
                -> YaEditController::GetParameterInfo::Response {
                const tresult result =
                    object_instances[request.instance_id]
                        .edit_controller->getParameterInfo(request.param_index,
                                                           request.info);

                return YaEditController::GetParameterInfoResponse{
                    .result = result, .updated_info = std::move(request.info)};
            },
            [&](const YaEditController::GetParamStringByValue& request)
                -> YaEditController::GetParamStringByValue::Response {
                Steinberg::Vst::String128 string{0};
                const tresult result =
                    object_instances[request.instance_id]
                        .edit_controller->getParamStringByValue(
                            request.id, request.value_normalized, string);

                return YaEditController::GetParamStringByValueResponse{
                    .result = result,
                    .string = tchar_pointer_to_u16string(string)};
            },
            [&](const YaEditController::GetParamValueByString& request)
                -> YaEditController::GetParamValueByString::Response {
                Steinberg::Vst::ParamValue value_normalized;
                const tresult result =
                    object_instances[request.instance_id]
                        .edit_controller->getParamValueByString(
                            request.id,
                            const_cast<Steinberg::Vst::TChar*>(
                                u16string_to_tchar_pointer(
                                    request.string.c_str())),
                            value_normalized);

                return YaEditController::GetParamValueByStringResponse{
                    .result = result, .value_normalized = value_normalized};
            },
            [&](const YaEditController::NormalizedParamToPlain& request)
                -> YaEditController::NormalizedParamToPlain::Response {
                return object_instances[request.instance_id]
                    .edit_controller->normalizedParamToPlain(
                        request.id, request.value_normalized);
            },
            [&](const YaEditController::PlainParamToNormalized& request)
                -> YaEditController::PlainParamToNormalized::Response {
                return object_instances[request.instance_id]
                    .edit_controller->plainParamToNormalized(
                        request.id, request.plain_value);
            },
            [&](const YaEditController::GetParamNormalized& request)
                -> YaEditController::GetParamNormalized::Response {
                return object_instances[request.instance_id]
                    .edit_controller->getParamNormalized(request.id);
            },
            [&](const YaEditController::SetParamNormalized& request)
                -> YaEditController::SetParamNormalized::Response {
                return object_instances[request.instance_id]
                    .edit_controller->setParamNormalized(request.id,
                                                         request.value);
            },
            [&](YaEditController::SetComponentHandler& request)
                -> YaEditController::SetComponentHandler::Response {
                // We'll create a proxy object for the component handler and
                // pass that to the initialize function. The lifetime of this
                // object is tied to that of the actual plugin object we're
                // proxying for.
                // TODO: Does this have to be run from the UI thread? Figure out
                //       if it does
                object_instances[request.instance_id].component_handler_proxy =
                    Steinberg::owned(new Vst3ComponentHandlerProxyImpl(
                        *this,
                        std::move(request.component_handler_proxy_args)));

                return object_instances[request.instance_id]
                    .edit_controller->setComponentHandler(
                        object_instances[request.instance_id]
                            .component_handler_proxy);
            },
            [&](const YaEditController::CreateView& request)
                -> YaEditController::CreateView::Response {
                // Instantiate the object from the GUI thread
                main_context
                    .run_in_context<void>([&]() {
                        object_instances[request.instance_id].plug_view =
                            Steinberg::owned(
                                object_instances[request.instance_id]
                                    .edit_controller->createView(
                                        request.name.c_str()));
                    })
                    .wait();

                // We'll create a proxy so the host can call functions on this
                // `IPlugView` object
                return YaEditController::CreateViewResponse{
                    .plug_view_args =
                        (object_instances[request.instance_id].plug_view
                             ? std::make_optional<
                                   Vst3PlugViewProxy::ConstructArgs>(
                                   object_instances[request.instance_id]
                                       .plug_view,
                                   request.instance_id)
                             : std::nullopt)};
            },
            [&](const YaEditController2::SetKnobMode& request)
                -> YaEditController2::SetKnobMode::Response {
                return object_instances[request.instance_id]
                    .edit_controller_2->setKnobMode(request.mode);
            },
            [&](const YaEditController2::OpenHelp& request)
                -> YaEditController2::OpenHelp::Response {
                return object_instances[request.instance_id]
                    .edit_controller_2->openHelp(request.only_check);
            },
            [&](const YaEditController2::OpenAboutBox& request)
                -> YaEditController2::OpenAboutBox::Response {
                return object_instances[request.instance_id]
                    .edit_controller_2->openAboutBox(request.only_check);
            },
            [&](const YaNoteExpressionController::GetNoteExpressionCount&
                    request)
                -> YaNoteExpressionController::GetNoteExpressionCount::
                    Response {
                        return object_instances[request.instance_id]
                            .note_expression_controller->getNoteExpressionCount(
                                request.bus_index, request.channel);
                    },
            [&](const YaNoteExpressionController::GetNoteExpressionInfo&
                    request)
                -> YaNoteExpressionController::GetNoteExpressionInfo::Response {
                Steinberg::Vst::NoteExpressionTypeInfo info{};
                const tresult result =
                    object_instances[request.instance_id]
                        .note_expression_controller->getNoteExpressionInfo(
                            request.bus_index, request.channel,
                            request.note_expression_index, info);

                return YaNoteExpressionController::
                    GetNoteExpressionInfoResponse{.result = result,
                                                  .info = std::move(info)};
            },
            [&](const YaNoteExpressionController::
                    GetNoteExpressionStringByValue& request)
                -> YaNoteExpressionController::GetNoteExpressionStringByValue::
                    Response {
                        Steinberg::Vst::String128 string{0};
                        const tresult result =
                            object_instances[request.instance_id]
                                .note_expression_controller
                                ->getNoteExpressionStringByValue(
                                    request.bus_index, request.channel,
                                    request.id, request.value_normalized,
                                    string);

                        return YaNoteExpressionController::
                            GetNoteExpressionStringByValueResponse{
                                .result = result,
                                .string = tchar_pointer_to_u16string(string)};
                    },
            [&](const YaNoteExpressionController::
                    GetNoteExpressionValueByString& request)
                -> YaNoteExpressionController::GetNoteExpressionValueByString::
                    Response {
                        Steinberg::Vst::NoteExpressionValue value_normalized;
                        const tresult result =
                            object_instances[request.instance_id]
                                .note_expression_controller
                                ->getNoteExpressionValueByString(
                                    request.bus_index, request.channel,
                                    request.id,
                                    u16string_to_tchar_pointer(request.string),
                                    value_normalized);

                        return YaNoteExpressionController::
                            GetNoteExpressionValueByStringResponse{
                                .result = result,
                                .value_normalized = value_normalized};
                    },
            [&](const YaPlugView::IsPlatformTypeSupported& request)
                -> YaPlugView::IsPlatformTypeSupported::Response {
                // The host will of course want to pass an X11 window ID for the
                // plugin to embed itself in, so we'll have to translate this to
                // a HWND
                const std::string type =
                    request.type == Steinberg::kPlatformTypeX11EmbedWindowID
                        ? Steinberg::kPlatformTypeHWND
                        : request.type;

                return object_instances[request.owner_instance_id]
                    .plug_view->isPlatformTypeSupported(type.c_str());
            },
            [&](const YaPlugView::Attached& request)
                -> YaPlugView::Attached::Response {
                const std::string type =
                    request.type == Steinberg::kPlatformTypeX11EmbedWindowID
                        ? Steinberg::kPlatformTypeHWND
                        : request.type;

                // Just like with VST2 plugins, we'll embed a Wine window into
                // the X11 window provided by the host
                // TODO: The docs say that we should support XEmbed (and we're
                //       purposely avoiding that because Wine's implementation
                //       doesn't work correctly). Check if this causes issues,
                //       and if it's actually needed (for instance when the host
                //       resizes the window without informing the plugin)
                const auto x11_handle = static_cast<size_t>(request.parent);

                // Creating the window and having the plugin embed in it should
                // be done in the main UI thread
                return main_context
                    .run_in_context<tresult>([&]() {
                        Editor& editor_instance =
                            object_instances[request.owner_instance_id]
                                .editor.emplace(config, x11_handle);

                        const tresult result =
                            object_instances[request.owner_instance_id]
                                .plug_view->attached(
                                    editor_instance.get_win32_handle(),
                                    type.c_str());

                        // Get rid of the editor again if the plugin didn't
                        // embed itself in it
                        if (result != Steinberg::kResultOk) {
                            object_instances[request.owner_instance_id]
                                .editor.reset();
                        }

                        return result;
                    })
                    .get();
            },
            [&](const YaPlugView::Removed& request)
                -> YaPlugView::Removed::Response {
                return main_context
                    .run_in_context<tresult>([&]() {
                        const tresult result =
                            object_instances[request.owner_instance_id]
                                .plug_view->removed();

                        object_instances[request.owner_instance_id]
                            .editor.reset();

                        return result;
                    })
                    .get();
            },
            [&](const YaPlugView::OnWheel& request)
                -> YaPlugView::OnWheel::Response {
                // Since all of these `IPlugView::on*` functions can cause a
                // redraw, they all have to be called from the UI thread
                return main_context
                    .run_in_context<tresult>([&]() {
                        return object_instances[request.owner_instance_id]
                            .plug_view->onWheel(request.distance);
                    })
                    .get();
            },
            [&](const YaPlugView::OnKeyDown& request)
                -> YaPlugView::OnKeyDown::Response {
                return main_context
                    .run_in_context<tresult>([&]() {
                        return object_instances[request.owner_instance_id]
                            .plug_view->onKeyDown(request.key, request.key_code,
                                                  request.modifiers);
                    })
                    .get();
            },
            [&](const YaPlugView::OnKeyUp& request)
                -> YaPlugView::OnKeyUp::Response {
                return main_context
                    .run_in_context<tresult>([&]() {
                        return object_instances[request.owner_instance_id]
                            .plug_view->onKeyUp(request.key, request.key_code,
                                                request.modifiers);
                    })
                    .get();
            },
            [&](YaPlugView::GetSize& request) -> YaPlugView::GetSize::Response {
                const tresult result =
                    object_instances[request.owner_instance_id]
                        .plug_view->getSize(&request.size);

                return YaPlugView::GetSizeResponse{
                    .result = result, .updated_size = std::move(request.size)};
            },
            [&](YaPlugView::OnSize& request) -> YaPlugView::OnSize::Response {
                // HACK: This function has to be run from the UI thread since
                //       the plugin probably wants to redraw when it gets
                //       resized. The issue here is that this function can be
                //       called in response to a call to
                //       `IPlugFrame::resizeView()`. That function is always
                //       called from the UI thread, so we need some way to run
                //       code on the same thread that's currently waiting for a
                //       response to the message it sent. See the docstring of
                //       this function for more information on how this works.
                return do_mutual_recursion_or_handle_in_main_context<tresult>(
                    [&]() {
                        return object_instances[request.owner_instance_id]
                            .plug_view->onSize(&request.new_size);
                    });
            },
            [&](const YaPlugView::OnFocus& request)
                -> YaPlugView::OnFocus::Response {
                return main_context
                    .run_in_context<tresult>([&]() {
                        return object_instances[request.owner_instance_id]
                            .plug_view->onFocus(request.state);
                    })
                    .get();
            },
            [&](YaPlugView::SetFrame& request)
                -> YaPlugView::SetFrame::Response {
                // We'll create a proxy object for the `IPlugFrame` object and
                // pass that to the `setFrame()` function. The lifetime of this
                // object is tied to that of the actual `IPlugFrame` object
                // we're passing this proxy to.
                object_instances[request.owner_instance_id].plug_frame_proxy =
                    Steinberg::owned(new Vst3PlugFrameProxyImpl(
                        *this, std::move(request.plug_frame_args)));

                // TODO: Does this have to be run from the UI thread? Figure out
                //       if it does
                return object_instances[request.owner_instance_id]
                    .plug_view->setFrame(
                        object_instances[request.owner_instance_id]
                            .plug_frame_proxy);
            },
            [&](YaPlugView::CanResize& request)
                -> YaPlugView::CanResize::Response {
                return object_instances[request.owner_instance_id]
                    .plug_view->canResize();
            },
            [&](YaPlugView::CheckSizeConstraint& request)
                -> YaPlugView::CheckSizeConstraint::Response {
                const tresult result =
                    object_instances[request.owner_instance_id]
                        .plug_view->checkSizeConstraint(&request.rect);

                return YaPlugView::CheckSizeConstraintResponse{
                    .result = result, .updated_rect = std::move(request.rect)};
            },
            [&](YaPluginBase::Initialize& request)
                -> YaPluginBase::Initialize::Response {
                // We'll create a proxy object for the host context passed by
                // the host and pass that to the initialize function. The
                // lifetime of this object is tied to that of the actual plugin
                // object we're proxying for.
                // TODO: This needs changing if it turns out we need a
                //       `Vst3HostProxy`
                object_instances[request.instance_id].host_context_proxy =
                    Steinberg::owned(new Vst3HostContextProxyImpl(
                        *this, std::move(request.host_context_args)));

                // XXX: Should `IPlugView::{initialize,terminate}` be run from
                //      the main UI thread? I can see how plugins would want to
                //      start timers from here.
                return main_context
                    .run_in_context<tresult>([&]() {
                        return object_instances[request.instance_id]
                            .plugin_base->initialize(
                                object_instances[request.instance_id]
                                    .host_context_proxy);
                    })
                    .get();
            },
            [&](const YaPluginBase::Terminate& request)
                -> YaPluginBase::Terminate::Response {
                return main_context
                    .run_in_context<tresult>([&]() {
                        return object_instances[request.instance_id]
                            .plugin_base->terminate();
                    })
                    .get();
            },
            [&](const YaProgramListData::ProgramDataSupported& request)
                -> YaProgramListData::ProgramDataSupported::Response {
                return object_instances[request.instance_id]
                    .program_list_data->programDataSupported(request.list_id);
            },
            [&](const YaProgramListData::GetProgramData& request)
                -> YaProgramListData::GetProgramData::Response {
                VectorStream data{};
                const tresult result =
                    object_instances[request.instance_id]
                        .program_list_data->getProgramData(
                            request.list_id, request.program_index, &data);

                return YaProgramListData::GetProgramDataResponse{
                    .result = result, .data = std::move(data)};
            },
            [&](YaProgramListData::SetProgramData& request)
                -> YaProgramListData::SetProgramData::Response {
                return object_instances[request.instance_id]
                    .program_list_data->setProgramData(
                        request.list_id, request.program_index, &request.data);
            },
            [&](const YaUnitData::UnitDataSupported& request)
                -> YaUnitData::UnitDataSupported::Response {
                return object_instances[request.instance_id]
                    .unit_data->unitDataSupported(request.unit_id);
            },
            [&](const YaUnitData::GetUnitData& request)
                -> YaUnitData::GetUnitData::Response {
                VectorStream data{};
                const tresult result =
                    object_instances[request.instance_id]
                        .unit_data->getUnitData(request.unit_id, &data);

                return YaUnitData::GetUnitDataResponse{.result = result,
                                                       .data = std::move(data)};
            },
            [&](YaUnitData::SetUnitData& request)
                -> YaUnitData::SetUnitData::Response {
                return object_instances[request.instance_id]
                    .unit_data->setUnitData(request.unit_id, &request.data);
            },
            [&](const YaPluginFactory::Construct&)
                -> YaPluginFactory::Construct::Response {
                return YaPluginFactory::ConstructArgs(
                    module->getFactory().get());
            },
            [&](YaPluginFactory::SetHostContext& request)
                -> YaPluginFactory::SetHostContext::Response {
                plugin_factory_host_context =
                    Steinberg::owned(new Vst3HostContextProxyImpl(
                        *this, std::move(request.host_context_args)));

                Steinberg::FUnknownPtr<Steinberg::IPluginFactory3> factory_3(
                    module->getFactory().get());
                assert(factory_3);

                return factory_3->setHostContext(plugin_factory_host_context);
            },
            [&](const YaUnitInfo::GetUnitCount& request)
                -> YaUnitInfo::GetUnitCount::Response {
                return object_instances[request.instance_id]
                    .unit_info->getUnitCount();
            },
            [&](const YaUnitInfo::GetUnitInfo& request)
                -> YaUnitInfo::GetUnitInfo::Response {
                Steinberg::Vst::UnitInfo info;
                const tresult result =
                    object_instances[request.instance_id]
                        .unit_info->getUnitInfo(request.unit_index, info);

                return YaUnitInfo::GetUnitInfoResponse{.result = result,
                                                       .info = std::move(info)};
            },
            [&](const YaUnitInfo::GetProgramListCount& request)
                -> YaUnitInfo::GetProgramListCount::Response {
                return object_instances[request.instance_id]
                    .unit_info->getProgramListCount();
            },
            [&](const YaUnitInfo::GetProgramListInfo& request)
                -> YaUnitInfo::GetProgramListInfo::Response {
                Steinberg::Vst::ProgramListInfo info;
                const tresult result = object_instances[request.instance_id]
                                           .unit_info->getProgramListInfo(
                                               request.list_index, info);

                return YaUnitInfo::GetProgramListInfoResponse{
                    .result = result, .info = std::move(info)};
            },
            [&](const YaUnitInfo::GetProgramName& request)
                -> YaUnitInfo::GetProgramName::Response {
                Steinberg::Vst::String128 name{0};
                const tresult result =
                    object_instances[request.instance_id]
                        .unit_info->getProgramName(request.list_id,
                                                   request.program_index, name);

                return YaUnitInfo::GetProgramNameResponse{
                    .result = result, .name = tchar_pointer_to_u16string(name)};
            },
            [&](const YaUnitInfo::GetProgramInfo& request)
                -> YaUnitInfo::GetProgramInfo::Response {
                Steinberg::Vst::String128 attribute_value{0};
                const tresult result =
                    object_instances[request.instance_id]
                        .unit_info->getProgramInfo(
                            request.list_id, request.program_index,
                            request.attribute_id.c_str(), attribute_value);

                return YaUnitInfo::GetProgramInfoResponse{
                    .result = result,
                    .attribute_value =
                        tchar_pointer_to_u16string(attribute_value)};
            },
            [&](const YaUnitInfo::HasProgramPitchNames& request)
                -> YaUnitInfo::HasProgramPitchNames::Response {
                return object_instances[request.instance_id]
                    .unit_info->hasProgramPitchNames(request.list_id,
                                                     request.program_index);
            },
            [&](const YaUnitInfo::GetProgramPitchName& request)
                -> YaUnitInfo::GetProgramPitchName::Response {
                Steinberg::Vst::String128 name{0};
                const tresult result =
                    object_instances[request.instance_id]
                        .unit_info->getProgramPitchName(
                            request.list_id, request.program_index,
                            request.midi_pitch, name);

                return YaUnitInfo::GetProgramPitchNameResponse{
                    .result = result, .name = tchar_pointer_to_u16string(name)};
            },
            [&](const YaUnitInfo::GetSelectedUnit& request)
                -> YaUnitInfo::GetSelectedUnit::Response {
                return object_instances[request.instance_id]
                    .unit_info->getSelectedUnit();
            },
            [&](const YaUnitInfo::SelectUnit& request)
                -> YaUnitInfo::SelectUnit::Response {
                return object_instances[request.instance_id]
                    .unit_info->selectUnit(request.unit_id);
            },
            [&](const YaUnitInfo::GetUnitByBus& request)
                -> YaUnitInfo::GetUnitByBus::Response {
                Steinberg::Vst::UnitID unit_id;
                const tresult result =
                    object_instances[request.instance_id]
                        .unit_info->getUnitByBus(request.type, request.dir,
                                                 request.bus_index,
                                                 request.channel, unit_id);

                return YaUnitInfo::GetUnitByBusResponse{.result = result,
                                                        .unit_id = unit_id};
            },
            [&](YaUnitInfo::SetUnitProgramData& request)
                -> YaUnitInfo::SetUnitProgramData::Response {
                return object_instances[request.instance_id]
                    .unit_info->setUnitProgramData(request.list_or_unit_id,
                                                   request.program_index,
                                                   &request.data);
            },
        });
}

void Vst3Bridge::handle_x11_events() {
    std::lock_guard lock(object_instances_mutex);

    for (const auto& [instance_id, object] : object_instances) {
        if (object.editor) {
            object.editor->handle_x11_events();
        }
    }
}

void Vst3Bridge::handle_win32_events() {
    MSG msg;

    for (int i = 0;
         i < max_win32_messages && PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE);
         i++) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

size_t Vst3Bridge::generate_instance_id() {
    return current_instance_id.fetch_add(1);
}

size_t Vst3Bridge::register_object_instance(
    Steinberg::IPtr<Steinberg::FUnknown> object) {
    std::lock_guard lock(object_instances_mutex);

    const size_t instance_id = generate_instance_id();
    object_instances.emplace(instance_id, std::move(object));

    // If the object supports `IComponent` or `IAudioProcessor`,
    // then we'll set up a dedicated thread for function calls for
    // those interfaces.
    if (object_instances[instance_id].audio_processor ||
        object_instances[instance_id].component) {
        std::promise<void> socket_listening_latch;

        object_instances[instance_id]
            .audio_processor_handler = Win32Thread([&, instance_id]() {
            sockets.add_audio_processor_and_listen(
                instance_id, socket_listening_latch,
                overload{
                    [&](YaAudioProcessor::SetBusArrangements& request)
                        -> YaAudioProcessor::SetBusArrangements::Response {
                        return object_instances[request.instance_id]
                            .audio_processor->setBusArrangements(
                                request.inputs.data(), request.num_ins,
                                request.outputs.data(), request.num_outs);
                    },
                    [&](YaAudioProcessor::GetBusArrangement& request)
                        -> YaAudioProcessor::GetBusArrangement::Response {
                        const tresult result =
                            object_instances[request.instance_id]
                                .audio_processor->getBusArrangement(
                                    request.dir, request.index, request.arr);

                        return YaAudioProcessor::GetBusArrangementResponse{
                            .result = result, .updated_arr = request.arr};
                    },
                    [&](const YaAudioProcessor::CanProcessSampleSize& request)
                        -> YaAudioProcessor::CanProcessSampleSize::Response {
                        return object_instances[request.instance_id]
                            .audio_processor->canProcessSampleSize(
                                request.symbolic_sample_size);
                    },
                    [&](const YaAudioProcessor::GetLatencySamples& request)
                        -> YaAudioProcessor::GetLatencySamples::Response {
                        return object_instances[request.instance_id]
                            .audio_processor->getLatencySamples();
                    },
                    [&](YaAudioProcessor::SetupProcessing& request)
                        -> YaAudioProcessor::SetupProcessing::Response {
                        return object_instances[request.instance_id]
                            .audio_processor->setupProcessing(request.setup);
                    },
                    [&](const YaAudioProcessor::SetProcessing& request)
                        -> YaAudioProcessor::SetProcessing::Response {
                        return object_instances[request.instance_id]
                            .audio_processor->setProcessing(request.state);
                    },
                    [&](YaAudioProcessor::Process& request)
                        -> YaAudioProcessor::Process::Response {
                        const tresult result =
                            object_instances[request.instance_id]
                                .audio_processor->process(request.data.get());

                        return YaAudioProcessor::ProcessResponse{
                            .result = result,
                            .output_data =
                                request.data.move_outputs_to_response()};
                    },
                    [&](const YaAudioProcessor::GetTailSamples& request)
                        -> YaAudioProcessor::GetTailSamples::Response {
                        return object_instances[request.instance_id]
                            .audio_processor->getTailSamples();
                    },
                    [&](const YaComponent::GetControllerClassId& request)
                        -> YaComponent::GetControllerClassId::Response {
                        Steinberg::TUID cid{0};
                        const tresult result =
                            object_instances[request.instance_id]
                                .component->getControllerClassId(cid);

                        return YaComponent::GetControllerClassIdResponse{
                            .result = result, .editor_cid = std::to_array(cid)};
                    },
                    [&](const YaComponent::SetIoMode& request)
                        -> YaComponent::SetIoMode::Response {
                        return object_instances[request.instance_id]
                            .component->setIoMode(request.mode);
                    },
                    [&](const YaComponent::GetBusCount& request)
                        -> YaComponent::GetBusCount::Response {
                        return object_instances[request.instance_id]
                            .component->getBusCount(request.type, request.dir);
                    },
                    [&](YaComponent::GetBusInfo& request)
                        -> YaComponent::GetBusInfo::Response {
                        const tresult result =
                            object_instances[request.instance_id]
                                .component->getBusInfo(
                                    request.type, request.dir, request.index,
                                    request.bus);

                        return YaComponent::GetBusInfoResponse{
                            .result = result, .updated_bus = request.bus};
                    },
                    [&](YaComponent::GetRoutingInfo& request)
                        -> YaComponent::GetRoutingInfo::Response {
                        const tresult result =
                            object_instances[request.instance_id]
                                .component->getRoutingInfo(request.in_info,
                                                           request.out_info);

                        return YaComponent::GetRoutingInfoResponse{
                            .result = result,
                            .updated_in_info = request.in_info,
                            .updated_out_info = request.out_info};
                    },
                    [&](const YaComponent::ActivateBus& request)
                        -> YaComponent::ActivateBus::Response {
                        return object_instances[request.instance_id]
                            .component->activateBus(request.type, request.dir,
                                                    request.index,
                                                    request.state);
                    },
                    [&](const YaComponent::SetActive& request)
                        -> YaComponent::SetActive::Response {
                        return object_instances[request.instance_id]
                            .component->setActive(request.state);
                    },
                });
        });

        // Wait for the new socket to be listening on before
        // continuing. Otherwise the native plugin may try to
        // connect to it before our thread is up and running.
        socket_listening_latch.get_future().wait();
    }

    return instance_id;
}

void Vst3Bridge::unregister_object_instance(size_t instance_id) {
    // Tear the dedicated audio processing socket down again if we
    // created one while handling `Vst3PluginProxy::Construct`
    if (object_instances[instance_id].audio_processor ||
        object_instances[instance_id].component) {
        sockets.remove_audio_processor(instance_id);
    }

    // Remove the instance from within the main IO context so
    // removing it doesn't interfere with the Win32 message loop
    // XXX: I don't think we have to wait for the object to be
    //      deleted most of the time, but I can imagine a situation
    //      where the plugin does a host callback triggered by a
    //      Win32 timer in between where the above closure is being
    //      executed and when the actual host application context on
    //      the plugin side gets deallocated.
    main_context
        .run_in_context<void>([&, instance_id]() {
            std::lock_guard lock(object_instances_mutex);
            object_instances.erase(instance_id);
        })
        .wait();
}
