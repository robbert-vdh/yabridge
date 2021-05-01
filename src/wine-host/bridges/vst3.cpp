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

#include "vst3-impls/component-handler-proxy.h"
#include "vst3-impls/connection-point-proxy.h"
#include "vst3-impls/context-menu-proxy.h"
#include "vst3-impls/host-context-proxy.h"
#include "vst3-impls/plug-frame-proxy.h"

// HACK: As of Wine commit `0c19e2e487d36a89531daf4897c0b6390d82a843` (or Wine
//       6.2), Wine's `shobjidl.h` cannot be compiled under C++ because one of
//       the parameters in the file operations interface is now named
//       `template`, which is a reserved keyword. Since we do not need this
//       interface, we'll just hack around this by making sure it never gets
//       defined.
//
//       https://bugs.winehq.org/show_bug.cgi?id=50670
#define __IFileOperation_INTERFACE_DEFINED__
// NOLINTNEXTLINE(bugprone-suspicious-include)
#include <public.sdk/source/vst/hosting/module_win32.cpp>

/**
 * This is a workaround for Bluecat Audio plugins that don't expose their
 * `IPluginBase` interface through the query interface. Even though every plugin
 * object _must_ support `IPlugBase`, these plugins only expose those functions
 * through `IComponent` (which derives from `IPluginBase`). So if we do
 * encounter one of those plugins, then we'll just have to coerce an
 * `IComponent` pointer into an `IPluginBase` smart pointer. This way we can
 * keep the rest of yabridge's design in tact.
 */
Steinberg::FUnknownPtr<Steinberg::IPluginBase> hack_init_plugin_base(
    Steinberg::IPtr<Steinberg::FUnknown> object,
    Steinberg::IPtr<Steinberg::Vst::IComponent> component);

InstancePlugView::InstancePlugView() {}

InstancePlugView::InstancePlugView(
    Steinberg::IPtr<Steinberg::IPlugView> plug_view)
    : plug_view(plug_view),
      parameter_finder(plug_view),
      plug_view_content_scale_support(plug_view) {}

InstanceInterfaces::InstanceInterfaces() {}

InstanceInterfaces::InstanceInterfaces(
    Steinberg::IPtr<Steinberg::FUnknown> object)
    : object(object),
      audio_presentation_latency(object),
      audio_processor(object),
      automation_state(object),
      component(object),
      connection_point(object),
      edit_controller(object),
      edit_controller_2(object),
      edit_controller_host_editing(object),
      info_listener(object),
      keyswitch_controller(object),
      midi_learn(object),
      midi_mapping(object),
      note_expression_controller(object),
      note_expression_physical_ui_mapping(object),
      plugin_base(hack_init_plugin_base(object, component)),
      unit_data(object),
      parameter_function_name(object),
      prefetchable_support(object),
      process_context_requirements(object),
      program_list_data(object),
      unit_info(object),
      xml_representation_controller(object),
      // If the object doesn't support `IPlugBase` then the object cannot be
      // uninitialized (this isn't possible right now, but, who knows what the
      // future might bring)
      is_initialized(!plugin_base) {}

Vst3Bridge::Vst3Bridge(MainContext& main_context,
                       std::string plugin_dll_path,
                       std::string endpoint_base_dir)
    : HostBridge(main_context, plugin_dll_path),
      logger(generic_logger),
      sockets(main_context.context, endpoint_base_dir, false) {
    std::string error;
    module = VST3::Hosting::Win32Module::create(plugin_dll_path, error);

    // HACK: If the plugin library was unable to load, then there's a tiny
    //       chance that the plugin expected the COM library to already be
    //       initialized. I've only seen PSPaudioware's InfiniStrip do this. In
    //       that case, we'll initialize the COM library for them and try again.
    if (!module) {
        OleInitialize(nullptr);
        module = VST3::Hosting::Win32Module::create(plugin_dll_path, error);
        if (module) {
            std::cerr << "WARNING: '" << plugin_dll_path << "'" << std::endl;
            std::cerr << "         could only load after we manually"
                      << std::endl;
            std::cerr << "         initialized the COM library." << std::endl;
        }
    }

    if (!module) {
        throw std::runtime_error("Could not load the VST3 module for '" +
                                 plugin_dll_path + "': " + error);
    }

    sockets.connect();

    // Fetch this instance's configuration from the plugin to finish the setup
    // process
    config = sockets.vst_host_callback.send_message(WantsConfiguration{},
                                                    std::nullopt);

    // Allow this plugin to configure the main context's tick rate
    main_context.update_timer_interval(config.event_loop_interval());
}

bool Vst3Bridge::inhibits_event_loop() {
    std::lock_guard lock(object_instances_mutex);

    for (const auto& [instance_id, object] : object_instances) {
        if (!object.is_initialized) {
            return true;
        }
    }

    return false;
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
            [&](const Vst3PluginFactoryProxy::Construct&)
                -> Vst3PluginFactoryProxy::Construct::Response {
                return Vst3PluginFactoryProxy::ConstructArgs(
                    module->getFactory().get());
            },
            [&](const Vst3PlugViewProxy::Destruct& request)
                -> Vst3PlugViewProxy::Destruct::Response {
                main_context
                    .run_in_context<void>([&]() {
                        // When the pointer gets dropped by the host, we want to
                        // drop it here as well, along with the `IPlugFrame`
                        // proxy object it may have received in
                        // `IPlugView::setFrame()`.
                        set_realtime_priority(false);
                        object_instances[request.owner_instance_id]
                            .plug_view_instance.reset();
                        object_instances[request.owner_instance_id]
                            .plug_frame_proxy.reset();
                        set_realtime_priority(true);
                    })
                    .wait();

                return Ack{};
            },
            [&](const Vst3PluginProxy::Construct& request)
                -> Vst3PluginProxy::Construct::Response {
                Steinberg::TUID cid;

                ArrayUID wine_cid = request.cid.get_wine_uid();
                std::copy(wine_cid.begin(), wine_cid.end(), cid);

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
                // We need to run `getState()` from the main thread, so we might
                // as well do the same thing with `setState()`. See below.
                // NOTE: We also try to handle mutual recursion here, in case
                //       this happens during a resize
                return do_mutual_recursion_on_gui_thread<tresult>([&]() {
                    // This same function is defined in both `IComponent` and
                    // `IEditController`, so the host is calling one or the
                    // other
                    if (object_instances[request.instance_id].component) {
                        return object_instances[request.instance_id]
                            .component->setState(&request.state);
                    } else {
                        return object_instances[request.instance_id]
                            .edit_controller->setState(&request.state);
                    }
                });
            },
            [&](Vst3PluginProxy::GetState& request)
                -> Vst3PluginProxy::GetState::Response {
                // NOTE: The VST3 version of Algonaut Atlas doesn't restore
                //       state unless this function is run from the GUI thread
                // NOTE: This also requires mutual recursion because REAPER will
                //       call `getState()` while opening a popup menu
                const tresult result =
                    do_mutual_recursion_on_gui_thread<tresult>([&]() {
                        // This same function is defined in both `IComponent`
                        // and `IEditController`, so the host is calling one or
                        // the other
                        if (object_instances[request.instance_id].component) {
                            return object_instances[request.instance_id]
                                .component->getState(&request.state);
                        } else {
                            return object_instances[request.instance_id]
                                .edit_controller->getState(&request.state);
                        }
                    });

                return Vst3PluginProxy::GetStateResponse{
                    .result = result, .state = std::move(request.state)};
            },
            [&](YaAudioPresentationLatency::SetAudioPresentationLatencySamples&
                    request)
                -> YaAudioPresentationLatency::
                    SetAudioPresentationLatencySamples::Response {
                        return object_instances[request.instance_id]
                            .audio_presentation_latency
                            ->setAudioPresentationLatencySamples(
                                request.dir, request.bus_index,
                                request.latency_in_samples);
                    },
            [&](YaAutomationState::SetAutomationState& request)
                -> YaAutomationState::SetAutomationState::Response {
                return object_instances[request.instance_id]
                    .automation_state->setAutomationState(request.state);
            },
            [&](YaConnectionPoint::Connect& request)
                -> YaConnectionPoint::Connect::Response {
                // If the host directly connected the underlying objects then we
                // can directly connect them as well. Some hosts, like Ardour
                // and Mixbus, will place a proxy between the two plugins This
                // can make things very complicated with FabFilter plugins,
                // which constantly communicate over this connection proxy from
                // the GUI thread. Because of that, we'll try to bypass the
                // connection proxy first, still connecting the objects directly
                // on the Wine side. If we cannot do that, then we'll still go
                // through the host's connection proxy connection proxy (and
                // we'll end up proxying the host's connection proxy).
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
                // NOTE: FabFilter plugins require some of their messages to be
                //       handled from the GUI thread. This could make the GUI
                //       much slower in Ardour, but there's no other non-hacky
                //       solution for this (and bypassing Ardour's connection
                //       proxies sort of goes against the idea behind yabridge)
                return do_mutual_recursion_on_gui_thread<tresult>([&]() {
                    return object_instances[request.instance_id]
                        .connection_point->notify(
                            request.message_ptr.get_original());
                });
            },
            [&](YaContextMenuTarget::ExecuteMenuItem& request)
                -> YaContextMenuTarget::ExecuteMenuItem::Response {
                return object_instances[request.owner_instance_id]
                    .registered_context_menus.at(request.context_menu_id)
                    .get()
                    .context_menu_targets[request.target_tag]
                    ->executeMenuItem(request.tag);
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
                Steinberg::Vst::ParameterInfo info{};
                const tresult result = object_instances[request.instance_id]
                                           .edit_controller->getParameterInfo(
                                               request.param_index, info);

                return YaEditController::GetParameterInfoResponse{
                    .result = result, .info = std::move(info)};
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
                // HACK: Under Ardour/Mixbus, `IComponentHandler::performEdit()`
                //       and `IEditController::setParamNormalized()` can be
                //       mutually recursive because the host will immediately
                //       relay the parameter change the plugin has just
                //       announced.
                return do_mutual_recursion_on_off_thread<tresult>([&]() {
                    return object_instances[request.instance_id]
                        .edit_controller->setParamNormalized(request.id,
                                                             request.value);
                });
            },
            [&](YaEditController::SetComponentHandler& request)
                -> YaEditController::SetComponentHandler::Response {
                // If the host passed a valid component handler, then we'll
                // create a proxy object for the component handler and pass that
                // to the initialize function. The lifetime of this object is
                // tied to that of the actual plugin object we're proxying for.
                // Otherwise we'll also pass a null pointer. This often happens
                // just before the host terminates the plugin.
                object_instances[request.instance_id].component_handler_proxy =
                    request.component_handler_proxy_args
                        ? Steinberg::owned(new Vst3ComponentHandlerProxyImpl(
                              *this,
                              std::move(*request.component_handler_proxy_args)))
                        : nullptr;

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
                        // NOTE: Just like in the event loop, we want to run
                        //       this with lower priority to prevent whatever
                        //       operation the plugin does while it's loading
                        //       its editor from preempting the audio thread.
                        set_realtime_priority(false);
                        object_instances[request.instance_id]
                            .plug_view_instance.emplace(Steinberg::owned(
                                object_instances[request.instance_id]
                                    .edit_controller->createView(
                                        request.name.c_str())));
                        set_realtime_priority(true);
                    })
                    .wait();

                // We'll create a proxy so the host can call functions on this
                // `IPlugView` object
                return YaEditController::CreateViewResponse{
                    .plug_view_args =
                        (object_instances[request.instance_id]
                                 .plug_view_instance
                             ? std::make_optional<
                                   Vst3PlugViewProxy::ConstructArgs>(
                                   object_instances[request.instance_id]
                                       .plug_view_instance->plug_view,
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
            [&](const YaEditControllerHostEditing::BeginEditFromHost& request)
                -> YaEditControllerHostEditing::BeginEditFromHost::Response {
                return object_instances[request.instance_id]
                    .edit_controller_host_editing->beginEditFromHost(
                        request.param_id);
            },
            [&](const YaEditControllerHostEditing::EndEditFromHost& request)
                -> YaEditControllerHostEditing::EndEditFromHost::Response {
                return object_instances[request.instance_id]
                    .edit_controller_host_editing->endEditFromHost(
                        request.param_id);
            },
            [&](YaInfoListener::SetChannelContextInfos& request)
                -> YaInfoListener::SetChannelContextInfos::Response {
                // Melodyne wants to immediately update the GUI upon receiving
                // certain channel context data, so this has to be run from the
                // main thread
                return main_context
                    .run_in_context<tresult>([&]() {
                        return object_instances[request.instance_id]
                            .info_listener->setChannelContextInfos(
                                &request.list);
                    })
                    .get();
            },
            [&](const YaKeyswitchController::GetKeyswitchCount& request)
                -> YaKeyswitchController::GetKeyswitchCount::Response {
                return object_instances[request.instance_id]
                    .keyswitch_controller->getKeyswitchCount(request.bus_index,
                                                             request.channel);
            },
            [&](const YaKeyswitchController::GetKeyswitchInfo& request)
                -> YaKeyswitchController::GetKeyswitchInfo::Response {
                Steinberg::Vst::KeyswitchInfo info{};
                const tresult result =
                    object_instances[request.instance_id]
                        .keyswitch_controller->getKeyswitchInfo(
                            request.bus_index, request.channel,
                            request.key_switch_index, info);

                return YaKeyswitchController::GetKeyswitchInfoResponse{
                    .result = result, .info = std::move(info)};
            },
            [&](const YaMidiLearn::OnLiveMIDIControllerInput& request)
                -> YaMidiLearn::OnLiveMIDIControllerInput::Response {
                return object_instances[request.instance_id]
                    .midi_learn->onLiveMIDIControllerInput(
                        request.bus_index, request.channel, request.midi_cc);
            },
            [&](const YaMidiMapping::GetMidiControllerAssignment& request)
                -> YaMidiMapping::GetMidiControllerAssignment::Response {
                Steinberg::Vst::ParamID id;
                const tresult result =
                    object_instances[request.instance_id]
                        .midi_mapping->getMidiControllerAssignment(
                            request.bus_index, request.channel,
                            request.midi_controller_number, id);

                return YaMidiMapping::GetMidiControllerAssignmentResponse{
                    .result = result, .id = id};
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
            [&](YaNoteExpressionPhysicalUIMapping::GetNotePhysicalUIMapping&
                    request)
                -> YaNoteExpressionPhysicalUIMapping::GetNotePhysicalUIMapping::
                    Response {
                        Steinberg::Vst::PhysicalUIMapList reconstructed_list =
                            request.list.get();
                        const tresult result =
                            object_instances[request.instance_id]
                                .note_expression_physical_ui_mapping
                                ->getPhysicalUIMapping(request.bus_index,
                                                       request.channel,
                                                       reconstructed_list);

                        return YaNoteExpressionPhysicalUIMapping::
                            GetNotePhysicalUIMappingResponse{
                                .result = result,
                                .list = std::move(request.list)};
                    },
            [&](const YaParameterFinder::FindParameter& request)
                -> YaParameterFinder::FindParameter::Response {
                Steinberg::Vst::ParamID result_tag;
                const tresult result =
                    object_instances[request.owner_instance_id]
                        .plug_view_instance->parameter_finder->findParameter(
                            request.x_pos, request.y_pos, result_tag);

                return YaParameterFinder::FindParameterResponse{
                    .result = result, .result_tag = result_tag};
            },
            [&](const YaParameterFunctionName::GetParameterIDFromFunctionName&
                    request) -> YaParameterFunctionName::
                                 GetParameterIDFromFunctionName::Response {
                                     Steinberg::Vst::ParamID param_id;
                                     const tresult result =
                                         object_instances[request.instance_id]
                                             .parameter_function_name
                                             ->getParameterIDFromFunctionName(
                                                 request.unit_id,
                                                 request.function_name.c_str(),
                                                 param_id);

                                     return YaParameterFunctionName::
                                         GetParameterIDFromFunctionNameResponse{
                                             .result = result,
                                             .param_id = param_id};
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
                    .plug_view_instance->plug_view->isPlatformTypeSupported(
                        type.c_str());
            },
            [&](const YaPlugView::Attached& request)
                -> YaPlugView::Attached::Response {
                const std::string type =
                    request.type == Steinberg::kPlatformTypeX11EmbedWindowID
                        ? Steinberg::kPlatformTypeHWND
                        : request.type;

                // Just like with VST2 plugins, we'll embed a Wine window into
                // the X11 window provided by the host
                const auto x11_handle = static_cast<size_t>(request.parent);

                // Creating the window and having the plugin embed in it should
                // be done in the main UI thread
                return main_context
                    .run_in_context<tresult>([&]() {
                        set_realtime_priority(false);
                        Editor& editor_instance =
                            object_instances[request.owner_instance_id]
                                .editor.emplace(main_context, config,
                                                x11_handle);
                        const tresult result =
                            object_instances[request.owner_instance_id]
                                .plug_view_instance->plug_view->attached(
                                    editor_instance.get_win32_handle(),
                                    type.c_str());
                        set_realtime_priority(true);

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
                        // Cleanup is handled through RAII
                        set_realtime_priority(false);
                        const tresult result =
                            object_instances[request.owner_instance_id]
                                .plug_view_instance->plug_view->removed();
                        object_instances[request.owner_instance_id]
                            .editor.reset();
                        set_realtime_priority(true);

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
                            .plug_view_instance->plug_view->onWheel(
                                request.distance);
                    })
                    .get();
            },
            [&](const YaPlugView::OnKeyDown& request)
                -> YaPlugView::OnKeyDown::Response {
                return main_context
                    .run_in_context<tresult>([&]() {
                        return object_instances[request.owner_instance_id]
                            .plug_view_instance->plug_view->onKeyDown(
                                request.key, request.key_code,
                                request.modifiers);
                    })
                    .get();
            },
            [&](const YaPlugView::OnKeyUp& request)
                -> YaPlugView::OnKeyUp::Response {
                return main_context
                    .run_in_context<tresult>([&]() {
                        return object_instances[request.owner_instance_id]
                            .plug_view_instance->plug_view->onKeyUp(
                                request.key, request.key_code,
                                request.modifiers);
                    })
                    .get();
            },
            [&](YaPlugView::GetSize& request) -> YaPlugView::GetSize::Response {
                // Melda plugins will refuse to open dialogs of this function is
                // not run from the GUI thread
                Steinberg::ViewRect size{};
                const tresult result =
                    do_mutual_recursion_on_gui_thread<tresult>([&]() {
                        return object_instances[request.owner_instance_id]
                            .plug_view_instance->plug_view->getSize(&size);
                    });

                return YaPlugView::GetSizeResponse{.result = result,
                                                   .size = std::move(size)};
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
                return do_mutual_recursion_on_gui_thread<tresult>([&]() {
                    return object_instances[request.owner_instance_id]
                        .plug_view_instance->plug_view->onSize(
                            &request.new_size);
                });
            },
            [&](const YaPlugView::OnFocus& request)
                -> YaPlugView::OnFocus::Response {
                return main_context
                    .run_in_context<tresult>([&]() {
                        return object_instances[request.owner_instance_id]
                            .plug_view_instance->plug_view->onFocus(
                                request.state);
                    })
                    .get();
            },
            [&](YaPlugView::SetFrame& request)
                -> YaPlugView::SetFrame::Response {
                // If the host passed a valid `IPlugFrame*`, then We'll create a
                // proxy object for the `IPlugFrame` object and pass that to the
                // `setFrame()` function. The lifetime of this object is tied to
                // that of the actual `IPlugFrame` object we're passing this
                // proxy to. IF the host passed a null pointer (which seems to
                // be common when terminating plugins) we'll do the same thing
                // here.
                object_instances[request.owner_instance_id].plug_frame_proxy =
                    request.plug_frame_args
                        ? Steinberg::owned(new Vst3PlugFrameProxyImpl(
                              *this, std::move(*request.plug_frame_args)))
                        : nullptr;

                // This likely doesn't have to be run from the GUI thread, but
                // since 80% of the `IPlugView` functions have to be we'll do it
                // here anyways
                return main_context
                    .run_in_context<tresult>([&]() {
                        return object_instances[request.owner_instance_id]
                            .plug_view_instance->plug_view->setFrame(
                                object_instances[request.owner_instance_id]
                                    .plug_frame_proxy);
                    })
                    .get();
            },
            [&](YaPlugView::CanResize& request)
                -> YaPlugView::CanResize::Response {
                // To prevent weird behaviour we'll perform all size related
                // functions from the GUI thread, including this one
                return do_mutual_recursion_on_gui_thread<tresult>([&]() {
                    return object_instances[request.owner_instance_id]
                        .plug_view_instance->plug_view->canResize();
                });
            },
            [&](YaPlugView::CheckSizeConstraint& request)
                -> YaPlugView::CheckSizeConstraint::Response {
                const tresult result =
                    do_mutual_recursion_on_gui_thread<tresult>([&]() {
                        return object_instances[request.owner_instance_id]
                            .plug_view_instance->plug_view->checkSizeConstraint(
                                &request.rect);
                    });

                return YaPlugView::CheckSizeConstraintResponse{
                    .result = result, .updated_rect = std::move(request.rect)};
            },
            [&](YaPlugViewContentScaleSupport::SetContentScaleFactor& request)
                -> YaPlugViewContentScaleSupport::SetContentScaleFactor::
                    Response {
                        if (config.vst3_no_scaling) {
                            std::cerr << "The host requested the editor GUI to "
                                         "be scaled by a factor of "
                                      << request.factor
                                      << ", but the 'vst3_no_scale' option is "
                                         "enabled. Ignoring the request."
                                      << std::endl;
                            return Steinberg::kNotImplemented;
                        } else {
                            return main_context
                                .run_in_context<tresult>([&]() {
                                    return object_instances
                                        [request.owner_instance_id]
                                            .plug_view_instance
                                            ->plug_view_content_scale_support
                                            ->setContentScaleFactor(
                                                request.factor);
                                })
                                .get();
                        }
                    },
            [&](YaPluginBase::Initialize& request)
                -> YaPluginBase::Initialize::Response {
                // We'll create a proxy object for the host context passed by
                // the host and pass that to the initialize function. The
                // lifetime of this object is tied to that of the actual plugin
                // object we're proxying for.
                object_instances[request.instance_id].host_context_proxy =
                    Steinberg::owned(new Vst3HostContextProxyImpl(
                        *this, std::move(request.host_context_args)));

                // Since plugins might want to start timers in
                // `IPlugView::{initialize,terminate}`, we'll run these
                // functions from the main GUI thread
                return main_context
                    .run_in_context<tresult>([&]() {
                        // This static cast is required to upcast to `FUnknown*`
                        const tresult result =
                            object_instances[request.instance_id]
                                .plugin_base->initialize(
                                    static_cast<YaHostApplication*>(
                                        object_instances[request.instance_id]
                                            .host_context_proxy));

                        // The Win32 message loop will not be run up to this
                        // point to prevent plugins with partially initialized
                        // states from misbehaving
                        object_instances[request.instance_id].is_initialized =
                            true;

                        return result;
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
            [&](const YaProcessContextRequirements::
                    GetProcessContextRequirements& request)
                -> YaProcessContextRequirements::GetProcessContextRequirements::
                    Response {
                        return object_instances[request.instance_id]
                            .process_context_requirements
                            ->getProcessContextRequirements();
                    },
            [&](YaProgramListData::GetProgramData& request)
                -> YaProgramListData::GetProgramData::Response {
                const tresult result =
                    object_instances[request.instance_id]
                        .program_list_data->getProgramData(
                            request.list_id, request.program_index,
                            &request.data);

                return YaProgramListData::GetProgramDataResponse{
                    .result = result, .data = std::move(request.data)};
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
            [&](YaUnitData::GetUnitData& request)
                -> YaUnitData::GetUnitData::Response {
                const tresult result =
                    object_instances[request.instance_id]
                        .unit_data->getUnitData(request.unit_id, &request.data);

                return YaUnitData::GetUnitDataResponse{
                    .result = result, .data = std::move(request.data)};
            },
            [&](YaUnitData::SetUnitData& request)
                -> YaUnitData::SetUnitData::Response {
                return object_instances[request.instance_id]
                    .unit_data->setUnitData(request.unit_id, &request.data);
            },
            [&](YaPluginFactory3::SetHostContext& request)
                -> YaPluginFactory3::SetHostContext::Response {
                plugin_factory_host_context =
                    Steinberg::owned(new Vst3HostContextProxyImpl(
                        *this, std::move(request.host_context_args)));

                Steinberg::FUnknownPtr<Steinberg::IPluginFactory3> factory_3(
                    module->getFactory().get());
                assert(factory_3);

                // This static cast is required to upcast to `FUnknown*`
                return factory_3->setHostContext(
                    static_cast<YaHostApplication*>(
                        plugin_factory_host_context));
            },
            [&](const YaUnitInfo::GetUnitCount& request)
                -> YaUnitInfo::GetUnitCount::Response {
                return object_instances[request.instance_id]
                    .unit_info->getUnitCount();
            },
            [&](const YaUnitInfo::GetUnitInfo& request)
                -> YaUnitInfo::GetUnitInfo::Response {
                Steinberg::Vst::UnitInfo info{};
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
                Steinberg::Vst::ProgramListInfo info{};
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
            [&](YaXmlRepresentationController::GetXmlRepresentationStream&
                    request) -> YaXmlRepresentationController::
                                 GetXmlRepresentationStream::Response {
                                     const tresult result =
                                         object_instances[request.instance_id]
                                             .xml_representation_controller
                                             ->getXmlRepresentationStream(
                                                 request.info, &request.stream);

                                     return YaXmlRepresentationController::
                                         GetXmlRepresentationStreamResponse{
                                             .result = result,
                                             .stream =
                                                 std::move(request.stream)};
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

void Vst3Bridge::close_sockets() {
    sockets.close();
}

void Vst3Bridge::register_context_menu(Vst3ContextMenuProxyImpl& context_menu) {
    std::lock_guard lock(object_instances[context_menu.owner_instance_id()]
                             .registered_context_menus_mutex);

    object_instances[context_menu.owner_instance_id()]
        .registered_context_menus.emplace(
            context_menu.context_menu_id(),
            std::ref<Vst3ContextMenuProxyImpl>(context_menu));
}

void Vst3Bridge::unregister_context_menu(size_t object_instance_id,
                                         size_t context_menu_id) {
    std::lock_guard lock(
        object_instances[object_instance_id].registered_context_menus_mutex);

    object_instances[object_instance_id].registered_context_menus.erase(
        context_menu_id);
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
                        // HACK: WA Production Imperfect VST3 somehow requires
                        //       `inputs` to be a valid pointer, even if there
                        //       are no inputs.
                        Steinberg::Vst::SpeakerArrangement empty_arrangement =
                            0b00000000;

                        return object_instances[request.instance_id]
                            .audio_processor->setBusArrangements(
                                request.num_ins > 0 ? request.inputs.data()
                                                    : &empty_arrangement,
                                request.num_ins,
                                request.num_outs > 0 ? request.outputs.data()
                                                     : &empty_arrangement,
                                request.num_outs);
                    },
                    [&](YaAudioProcessor::GetBusArrangement& request)
                        -> YaAudioProcessor::GetBusArrangement::Response {
                        Steinberg::Vst::SpeakerArrangement arr{};
                        const tresult result =
                            object_instances[request.instance_id]
                                .audio_processor->getBusArrangement(
                                    request.dir, request.index, arr);

                        return YaAudioProcessor::GetBusArrangementResponse{
                            .result = result, .arr = arr};
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
                        // Most plugins will already enable FTZ, but there are a
                        // handful of plugins that don't that suffer from
                        // extreme DSP load increases when they start producing
                        // denormals
                        ScopedFlushToZero ftz_guard;

                        // As suggested by Jack Winter, we'll synchronize this
                        // thread's audio processing priority with that of the
                        // host's audio thread every once in a while
                        if (request.new_realtime_priority) {
                            set_realtime_priority(
                                true, *request.new_realtime_priority);
                        }

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
                            .result = result, .editor_cid = cid};
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
                        Steinberg::Vst::BusInfo bus{};
                        const tresult result =
                            object_instances[request.instance_id]
                                .component->getBusInfo(request.type,
                                                       request.dir,
                                                       request.index, bus);

                        return YaComponent::GetBusInfoResponse{
                            .result = result, .bus = std::move(bus)};
                    },
                    [&](YaComponent::GetRoutingInfo& request)
                        -> YaComponent::GetRoutingInfo::Response {
                        Steinberg::Vst::RoutingInfo out_info{};
                        const tresult result =
                            object_instances[request.instance_id]
                                .component->getRoutingInfo(request.in_info,
                                                           out_info);

                        return YaComponent::GetRoutingInfoResponse{
                            .result = result, .out_info = std::move(out_info)};
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
                        // NOTE: Ardour/Mixbus will immediately call this
                        //       function in response to a latency change
                        //       announced through
                        //       `IComponentHandler::restartComponent()`. We
                        //       need to make sure that these two functions are
                        //       handled from the same thread to prevent
                        //       deadlocks caused by mutually recursive function
                        //       calls.
                        return do_mutual_recursion_on_off_thread<tresult>(
                            [&]() {
                                return object_instances[request.instance_id]
                                    .component->setActive(request.state);
                            });
                    },
                    [&](const YaPrefetchableSupport::GetPrefetchableSupport&
                            request)
                        -> YaPrefetchableSupport::GetPrefetchableSupport::
                            Response {
                                Steinberg::Vst::PrefetchableSupport
                                    prefetchable;
                                const tresult result =
                                    object_instances[request.instance_id]
                                        .prefetchable_support
                                        ->getPrefetchableSupport(prefetchable);

                                return YaPrefetchableSupport::
                                    GetPrefetchableSupportResponse{
                                        .result = result,
                                        .prefetchable = prefetchable};
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

Steinberg::FUnknownPtr<Steinberg::IPluginBase> hack_init_plugin_base(
    Steinberg::IPtr<Steinberg::FUnknown> object,
    Steinberg::IPtr<Steinberg::Vst::IComponent> component) {
    // See the docstring for more information
    Steinberg::FUnknownPtr<Steinberg::IPluginBase> plugin_base(object);
    if (plugin_base) {
        return plugin_base;
    } else if (component) {
        // HACK: So this should never be hit, because every object
        //       initializeable from a plugin's factory must inherit from
        //       `IPluginBase`. But, the Bluecat Audio plugins seem to have an
        //       implementation issue where they don't expose this interface. So
        //       instead we'll coerce from `IComponent` instead if this is the
        //       case, since `IComponent` derives from `IPluginBase`. Doing
        //       these manual pointer casts should be perfectly safe, even if
        //       they go against the very idea of having a query interface.
        static_assert(sizeof(Steinberg::FUnknownPtr<Steinberg::IPluginBase>) ==
                      sizeof(Steinberg::IPtr<Steinberg::IPluginBase>));

        std::cerr << "WARNING: This plugin doesn't expose the IPluginBase"
                  << std::endl;
        std::cerr << "         interface and is broken. We will attempt an"
                  << std::endl;
        std::cerr << "         unsafe coercion from IComponent instead."
                  << std::endl;

        Steinberg::IPtr<Steinberg::IPluginBase> coerced_plugin_base(
            component.get());

        return *static_cast<Steinberg::FUnknownPtr<Steinberg::IPluginBase>*>(
            &coerced_plugin_base);
    } else {
        // This isn't really needed because the VST3 smart pointers can already
        // deal with null pointers, but might as well drive the point of this
        // hack home even further
        return nullptr;
    }
}
