// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2022 Robbert van der Helm
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

#include <bitset>

#include "vst3-impls/component-handler-proxy.h"
#include "vst3-impls/connection-point-proxy.h"
#include "vst3-impls/context-menu-proxy.h"
#include "vst3-impls/host-context-proxy.h"
#include "vst3-impls/plug-frame-proxy.h"

// Generated inside of the build directory
#include <version.h>

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

Vst3PlugViewInterfaces::Vst3PlugViewInterfaces() noexcept {}

Vst3PlugViewInterfaces::Vst3PlugViewInterfaces(
    Steinberg::IPtr<Steinberg::IPlugView> plug_view) noexcept
    : plug_view(plug_view),
      parameter_finder(plug_view),
      plug_view_content_scale_support(plug_view) {}

Vst3PluginInterfaces::Vst3PluginInterfaces(
    Steinberg::IPtr<Steinberg::FUnknown> object) noexcept
    : audio_presentation_latency(object),
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
      xml_representation_controller(object) {}

Vst3PluginInstance::Vst3PluginInstance(
    Steinberg::IPtr<Steinberg::FUnknown> object) noexcept
    : object(object),
      interfaces(object),
      // If the object doesn't support `IPlugBase` then the object cannot be
      // uninitialized (this isn't possible right now, but, who knows what the
      // future might bring)
      is_initialized(!interfaces.plugin_base) {}

Vst3Bridge::Vst3Bridge(MainContext& main_context,
                       // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
                       std::string plugin_dll_path,
                       std::string endpoint_base_dir,
                       pid_t parent_pid)
    : HostBridge(main_context, plugin_dll_path, parent_pid),
      logger_(generic_logger_),
      sockets_(main_context.context_, endpoint_base_dir, false) {
    std::string error;
    module_ = VST3::Hosting::Win32Module::create(plugin_dll_path, error);
    if (!module_) {
        throw std::runtime_error("Could not load the VST3 module for '" +
                                 plugin_dll_path + "': " + error);
    }

    sockets_.connect();

    // Fetch this instance's configuration from the plugin to finish the setup
    // process
    config_ = sockets_.vst_host_callback_.send_message(
        WantsConfiguration{.host_version = yabridge_git_version}, std::nullopt);

    // Allow this plugin to configure the main context's tick rate
    main_context.update_timer_interval(config_.event_loop_interval());
}

bool Vst3Bridge::inhibits_event_loop() noexcept {
    std::shared_lock lock(object_instances_mutex_);

    for (const auto& [instance_id, instance] : object_instances_) {
        if (!instance.is_initialized) {
            return true;
        }
    }

    return false;
}

void Vst3Bridge::run() {
    set_realtime_priority(true);

    sockets_.host_vst_control_.receive_messages(
        std::nullopt,
        overload{
            [&](const Vst3PluginFactoryProxy::Construct&)
                -> Vst3PluginFactoryProxy::Construct::Response {
                return Vst3PluginFactoryProxy::ConstructArgs(
                    module_->getFactory().get());
            },
            [&](const Vst3PlugViewProxy::Destruct& request)
                -> Vst3PlugViewProxy::Destruct::Response {
                main_context_
                    .run_in_context([&]() -> void {
                        // When the pointer gets dropped by the host, we want to
                        // drop it here as well, along with the `IPlugFrame`
                        // proxy object it may have received in
                        // `IPlugView::setFrame()`.
                        const auto& [instance, _] =
                            get_instance(request.owner_instance_id);

                        instance.plug_view_instance.reset();
                        instance.plug_frame_proxy.reset();
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
                    main_context_
                        .run_in_context(
                            [&]() -> Steinberg::IPtr<Steinberg::FUnknown> {
                                Steinberg::IPtr<Steinberg::FUnknown> result;

                                // The plugin may spawn audio worker threads
                                // when constructing an object. Since Wine
                                // doesn't implement Window's realtime process
                                // priority yet we'll just have to make sure the
                                // any spawned threads are running with
                                // `SCHED_FIFO` ourselves.
                                set_realtime_priority(true);
                                switch (request.requested_interface) {
                                    case Vst3PluginProxy::Construct::Interface::
                                        IComponent:
                                        result =
                                            module_->getFactory()
                                                .createInstance<
                                                    Steinberg::Vst::IComponent>(
                                                    cid);
                                        break;
                                    case Vst3PluginProxy::Construct::Interface::
                                        IEditController:
                                        result =
                                            module_->getFactory()
                                                .createInstance<
                                                    Steinberg::Vst::
                                                        IEditController>(cid);
                                        break;
                                    default:
                                        // Unreachable
                                        result = nullptr;
                                        break;
                                }
                                set_realtime_priority(false);

                                return result;
                            })
                        .get();

                if (!object) {
                    return UniversalTResult(Steinberg::kResultFalse);
                }

                const size_t instance_id = register_object_instance(object);
                const auto& [instance, _] = get_instance(instance_id);

                // This is where the magic happens. Here we deduce which
                // interfaces are supported by this object so we can create
                // a one-to-one proxy of it.
                return Vst3PluginProxy::ConstructArgs(instance.object,
                                                      instance_id);
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
                return do_mutual_recursion_on_gui_thread([&]() -> tresult {
                    const auto& [instance, _] =
                        get_instance(request.instance_id);

                    // This same function is defined in both `IComponent` and
                    // `IEditController`, so the host is calling one or the
                    // other
                    if (instance.interfaces.component) {
                        return instance.interfaces.component->setState(
                            &request.state);
                    } else {
                        return instance.interfaces.edit_controller->setState(
                            &request.state);
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
                    do_mutual_recursion_on_gui_thread([&]() -> tresult {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        // This same function is defined in both `IComponent`
                        // and `IEditController`, so the host is calling one or
                        // the other
                        if (instance.interfaces.component) {
                            return instance.interfaces.component->getState(
                                &request.state);
                        } else {
                            return instance.interfaces.edit_controller
                                ->getState(&request.state);
                        }
                    });

                return Vst3PluginProxy::GetStateResponse{
                    .result = result, .state = std::move(request.state)};
            },
            [&](YaAudioPresentationLatency::SetAudioPresentationLatencySamples&
                    request) -> YaAudioPresentationLatency::
                                 SetAudioPresentationLatencySamples::Response {
                                     const auto& [instance, _] =
                                         get_instance(request.instance_id);

                                     return instance.interfaces
                                         .audio_presentation_latency
                                         ->setAudioPresentationLatencySamples(
                                             request.dir, request.bus_index,
                                             request.latency_in_samples);
                                 },
            [&](YaAutomationState::SetAutomationState& request)
                -> YaAutomationState::SetAutomationState::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.automation_state->setAutomationState(
                    request.state);
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
                            const auto& [this_instance, _] =
                                get_instance(request.instance_id);
                            const auto& [other_instance, _2] =
                                get_instance(other_instance_id);

                            return this_instance.interfaces.connection_point
                                ->connect(
                                    other_instance.interfaces.connection_point);
                        },
                        [&](Vst3ConnectionPointProxy::ConstructArgs& args)
                            -> tresult {
                            const auto& [this_instance, _] =
                                get_instance(request.instance_id);

                            this_instance.connection_point_proxy =
                                Steinberg::owned(
                                    new Vst3ConnectionPointProxyImpl(
                                        *this, std::move(args)));

                            return this_instance.interfaces.connection_point
                                ->connect(this_instance.connection_point_proxy);
                        }},
                    request.other);
            },
            [&](const YaConnectionPoint::Disconnect& request)
                -> YaConnectionPoint::Disconnect::Response {
                const auto& [this_instance, _] =
                    get_instance(request.instance_id);

                // If the objects were connected directly we can also disconnect
                // them directly. Otherwise we'll disconnect them from our proxy
                // object and then destroy that proxy object.
                if (request.other_instance_id) {
                    const auto& [other_instance, _2] =
                        get_instance(*request.other_instance_id);

                    return this_instance.interfaces.connection_point
                        ->disconnect(
                            other_instance.interfaces.connection_point);
                } else {
                    const tresult result =
                        this_instance.interfaces.connection_point->disconnect(
                            this_instance.connection_point_proxy);
                    this_instance.connection_point_proxy.reset();

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
                return do_mutual_recursion_on_gui_thread([&]() -> tresult {
                    const auto& [this_instance, _] =
                        get_instance(request.instance_id);

                    return this_instance.interfaces.connection_point->notify(
                        request.message_ptr.get_original());
                });
            },
            [&](YaContextMenuTarget::ExecuteMenuItem& request)
                -> YaContextMenuTarget::ExecuteMenuItem::Response {
                const auto& [instance, _] =
                    get_instance(request.owner_instance_id);

                // This is of course only used for calling plugin defined
                // targets from the host, this will never be called when the
                // host calls their own targets for whatever reason
                return instance.registered_context_menus
                    .at(request.context_menu_id)
                    .get()
                    .plugin_targets_[request.target_tag]
                    ->executeMenuItem(request.tag);
            },
            [&](YaEditController::SetComponentState& request)
                -> YaEditController::SetComponentState::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.edit_controller->setComponentState(
                    &request.state);
            },
            [&](const YaEditController::GetParameterCount& request)
                -> YaEditController::GetParameterCount::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.edit_controller->getParameterCount();
            },
            [&](YaEditController::GetParameterInfo& request)
                -> YaEditController::GetParameterInfo::Response {
                Steinberg::Vst::ParameterInfo info{};
                const auto& [instance, _] = get_instance(request.instance_id);

                const tresult result =
                    instance.interfaces.edit_controller->getParameterInfo(
                        request.param_index, info);

                return YaEditController::GetParameterInfoResponse{
                    .result = result, .info = std::move(info)};
            },
            [&](const YaEditController::GetParamStringByValue& request)
                -> YaEditController::GetParamStringByValue::Response {
                Steinberg::Vst::String128 string{0};
                const auto& [instance, _] = get_instance(request.instance_id);

                const tresult result =
                    instance.interfaces.edit_controller->getParamStringByValue(
                        request.id, request.value_normalized, string);

                return YaEditController::GetParamStringByValueResponse{
                    .result = result,
                    .string = tchar_pointer_to_u16string(string)};
            },
            [&](const YaEditController::GetParamValueByString& request)
                -> YaEditController::GetParamValueByString::Response {
                Steinberg::Vst::ParamValue value_normalized;
                const auto& [instance, _] = get_instance(request.instance_id);

                const tresult result =
                    instance.interfaces.edit_controller->getParamValueByString(
                        request.id,
                        const_cast<Steinberg::Vst::TChar*>(
                            u16string_to_tchar_pointer(request.string)),
                        value_normalized);

                return YaEditController::GetParamValueByStringResponse{
                    .result = result, .value_normalized = value_normalized};
            },
            [&](const YaEditController::NormalizedParamToPlain& request)
                -> YaEditController::NormalizedParamToPlain::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.edit_controller
                    ->normalizedParamToPlain(request.id,
                                             request.value_normalized);
            },
            [&](const YaEditController::PlainParamToNormalized& request)
                -> YaEditController::PlainParamToNormalized::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.edit_controller
                    ->plainParamToNormalized(request.id, request.plain_value);
            },
            [&](const YaEditController::GetParamNormalized& request)
                -> YaEditController::GetParamNormalized::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.edit_controller->getParamNormalized(
                    request.id);
            },
            [&](const YaEditController::SetParamNormalized& request)
                -> YaEditController::SetParamNormalized::Response {
                // HACK: Under Ardour/Mixbus, `IComponentHandler::performEdit()`
                //       and `IEditController::setParamNormalized()` can be
                //       mutually recursive because the host will immediately
                //       relay the parameter change the plugin has just
                //       announced.
                return do_mutual_recursion_on_off_thread([&]() -> tresult {
                    const auto& [instance, _] =
                        get_instance(request.instance_id);

                    return instance.interfaces.edit_controller
                        ->setParamNormalized(request.id, request.value);
                });
            },
            [&](YaEditController::SetComponentHandler& request)
                -> YaEditController::SetComponentHandler::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                // If the host passed a valid component handler, then we'll
                // create a proxy object for the component handler and pass that
                // to the initialize function. The lifetime of this object is
                // tied to that of the actual plugin object we're proxying for.
                // Otherwise we'll also pass a null pointer. This often happens
                // just before the host terminates the plugin.
                instance.component_handler_proxy =
                    request.component_handler_proxy_args
                        ? Steinberg::owned(new Vst3ComponentHandlerProxyImpl(
                              *this,
                              std::move(*request.component_handler_proxy_args)))
                        : nullptr;

                return instance.interfaces.edit_controller->setComponentHandler(
                    instance.component_handler_proxy);
            },
            [&](const YaEditController::CreateView& request)
                -> YaEditController::CreateView::Response {
                // Instantiate the object from the GUI thread
                const auto plug_view_args =
                    main_context_
                        .run_in_context(
                            [&]() -> std::optional<
                                      Vst3PlugViewProxy::ConstructArgs> {
                                const auto& [instance, _] =
                                    get_instance(request.instance_id);

                                Steinberg::IPtr<Steinberg::IPlugView> plug_view(
                                    Steinberg::owned(
                                        instance.interfaces.edit_controller
                                            ->createView(
                                                request.name.c_str())));

                                if (plug_view) {
                                    instance.plug_view_instance.emplace(
                                        plug_view);

                                    // We'll create a proxy so the host can call
                                    // functions on this `IPlugView` object
                                    return std::make_optional<
                                        Vst3PlugViewProxy::ConstructArgs>(
                                        instance.plug_view_instance->plug_view,
                                        request.instance_id);
                                } else {
                                    instance.plug_view_instance.reset();

                                    return std::nullopt;
                                }
                            })
                        .get();

                return YaEditController::CreateViewResponse{.plug_view_args =
                                                                plug_view_args};
            },
            [&](const YaEditController2::SetKnobMode& request)
                -> YaEditController2::SetKnobMode::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.edit_controller_2->setKnobMode(
                    request.mode);
            },
            [&](const YaEditController2::OpenHelp& request)
                -> YaEditController2::OpenHelp::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.edit_controller_2->openHelp(
                    request.only_check);
            },
            [&](const YaEditController2::OpenAboutBox& request)
                -> YaEditController2::OpenAboutBox::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.edit_controller_2->openAboutBox(
                    request.only_check);
            },
            [&](const YaEditControllerHostEditing::BeginEditFromHost& request)
                -> YaEditControllerHostEditing::BeginEditFromHost::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.edit_controller_host_editing
                    ->beginEditFromHost(request.param_id);
            },
            [&](const YaEditControllerHostEditing::EndEditFromHost& request)
                -> YaEditControllerHostEditing::EndEditFromHost::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.edit_controller_host_editing
                    ->endEditFromHost(request.param_id);
            },
            [&](YaInfoListener::SetChannelContextInfos& request)
                -> YaInfoListener::SetChannelContextInfos::Response {
                // Melodyne wants to immediately update the GUI upon receiving
                // certain channel context data, so this has to be run from the
                // main thread
                return main_context_
                    .run_in_context([&]() -> tresult {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        return instance.interfaces.info_listener
                            ->setChannelContextInfos(&request.list);
                    })
                    .get();
            },
            [&](const YaKeyswitchController::GetKeyswitchCount& request)
                -> YaKeyswitchController::GetKeyswitchCount::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.keyswitch_controller
                    ->getKeyswitchCount(request.bus_index, request.channel);
            },
            [&](const YaKeyswitchController::GetKeyswitchInfo& request)
                -> YaKeyswitchController::GetKeyswitchInfo::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                Steinberg::Vst::KeyswitchInfo info{};
                const tresult result =
                    instance.interfaces.keyswitch_controller->getKeyswitchInfo(
                        request.bus_index, request.channel,
                        request.key_switch_index, info);

                return YaKeyswitchController::GetKeyswitchInfoResponse{
                    .result = result, .info = std::move(info)};
            },
            [&](const YaMidiLearn::OnLiveMIDIControllerInput& request)
                -> YaMidiLearn::OnLiveMIDIControllerInput::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.midi_learn
                    ->onLiveMIDIControllerInput(
                        request.bus_index, request.channel, request.midi_cc);
            },
            [&](const YaMidiMapping::GetMidiControllerAssignment& request)
                -> YaMidiMapping::GetMidiControllerAssignment::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                Steinberg::Vst::ParamID id;
                const tresult result =
                    instance.interfaces.midi_mapping
                        ->getMidiControllerAssignment(
                            request.bus_index, request.channel,
                            request.midi_controller_number, id);

                return YaMidiMapping::GetMidiControllerAssignmentResponse{
                    .result = result, .id = id};
            },
            [&](const YaNoteExpressionController::GetNoteExpressionCount&
                    request)
                -> YaNoteExpressionController::GetNoteExpressionCount::
                    Response {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        return instance.interfaces.note_expression_controller
                            ->getNoteExpressionCount(request.bus_index,
                                                     request.channel);
                    },
            [&](const YaNoteExpressionController::GetNoteExpressionInfo&
                    request)
                -> YaNoteExpressionController::GetNoteExpressionInfo::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                Steinberg::Vst::NoteExpressionTypeInfo info{};
                const tresult result =
                    instance.interfaces.note_expression_controller
                        ->getNoteExpressionInfo(
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
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        Steinberg::Vst::String128 string{0};
                        const tresult result =
                            instance.interfaces.note_expression_controller
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
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        Steinberg::Vst::NoteExpressionValue value_normalized;
                        const tresult result =
                            instance.interfaces.note_expression_controller
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
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        Steinberg::Vst::PhysicalUIMapList reconstructed_list =
                            request.list.get();
                        const tresult result =
                            instance.interfaces
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
                const auto& [instance, _] =
                    get_instance(request.owner_instance_id);

                Steinberg::Vst::ParamID result_tag;
                const tresult result =
                    instance.plug_view_instance->parameter_finder
                        ->findParameter(request.x_pos, request.y_pos,
                                        result_tag);

                return YaParameterFinder::FindParameterResponse{
                    .result = result, .result_tag = result_tag};
            },
            [&](const YaParameterFunctionName::GetParameterIDFromFunctionName&
                    request)
                -> YaParameterFunctionName::GetParameterIDFromFunctionName::
                    Response {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        Steinberg::Vst::ParamID param_id;
                        const tresult result =
                            instance.interfaces.parameter_function_name
                                ->getParameterIDFromFunctionName(
                                    request.unit_id,
                                    request.function_name.c_str(), param_id);

                        return YaParameterFunctionName::
                            GetParameterIDFromFunctionNameResponse{
                                .result = result, .param_id = param_id};
                    },
            [&](const YaPlugView::IsPlatformTypeSupported& request)
                -> YaPlugView::IsPlatformTypeSupported::Response {
                const auto& [instance, _] =
                    get_instance(request.owner_instance_id);

                // The host will of course want to pass an X11 window ID for the
                // plugin to embed itself in, so we'll have to translate this to
                // a HWND
                const std::string type =
                    request.type == Steinberg::kPlatformTypeX11EmbedWindowID
                        ? Steinberg::kPlatformTypeHWND
                        : request.type;

                return instance.plug_view_instance->plug_view
                    ->isPlatformTypeSupported(type.c_str());
            },
            [&](const YaPlugView::Attached& request)
                -> YaPlugView::Attached::Response {
                const auto& [instance, _] =
                    get_instance(request.owner_instance_id);

                const std::string type =
                    request.type == Steinberg::kPlatformTypeX11EmbedWindowID
                        ? Steinberg::kPlatformTypeHWND
                        : request.type;

                // Just like with VST2 plugins, we'll embed a Wine window into
                // the X11 window provided by the host
                const auto x11_handle = static_cast<size_t>(request.parent);

                // Creating the window and having the plugin embed in it should
                // be done in the main UI thread
                return main_context_
                    .run_in_context([&, &instance = instance]() -> tresult {
                        Editor& editor_instance = instance.editor.emplace(
                            main_context_, config_, generic_logger_,
                            x11_handle);
                        const tresult result =
                            instance.plug_view_instance->plug_view->attached(
                                editor_instance.get_win32_handle(),
                                type.c_str());

                        // Set the window's initial size according to what the
                        // plugin reports. Otherwise get rid of the editor again
                        // if the plugin didn't embed itself in it.
                        if (result == Steinberg::kResultOk) {
                            Steinberg::ViewRect size{};
                            if (instance.plug_view_instance->plug_view->getSize(
                                    &size) == Steinberg::kResultOk) {
                                instance.editor->resize(size.getWidth(),
                                                        size.getHeight());
                            }

                            // NOTE: There's zero reason why the window couldn't
                            //       already be visible from the start, but
                            //       Waves V13 VST3 plugins think it would be a
                            //       splendid idea to randomly dereference null
                            //       pointers when the window is already
                            //       visible. Thanks Waves.
                            instance.editor->show();
                        } else {
                            instance.editor.reset();
                        }

                        return result;
                    })
                    .get();
            },
            [&](const YaPlugView::Removed& request)
                -> YaPlugView::Removed::Response {
                return main_context_
                    .run_in_context([&]() -> tresult {
                        const auto& [instance, _] =
                            get_instance(request.owner_instance_id);

                        // Cleanup is handled through RAII
                        const tresult result =
                            instance.plug_view_instance->plug_view->removed();
                        instance.editor.reset();

                        return result;
                    })
                    .get();
            },
            [&](const YaPlugView::OnWheel& request)
                -> YaPlugView::OnWheel::Response {
                // Since all of these `IPlugView::on*` functions can cause a
                // redraw, they all have to be called from the UI thread
                return main_context_
                    .run_in_context([&]() -> tresult {
                        const auto& [instance, _] =
                            get_instance(request.owner_instance_id);

                        return instance.plug_view_instance->plug_view->onWheel(
                            request.distance);
                    })
                    .get();
            },
            [&](const YaPlugView::OnKeyDown& request)
                -> YaPlugView::OnKeyDown::Response {
                return main_context_
                    .run_in_context([&]() -> tresult {
                        const auto& [instance, _] =
                            get_instance(request.owner_instance_id);

                        return instance.plug_view_instance->plug_view
                            ->onKeyDown(request.key, request.key_code,
                                        request.modifiers);
                    })
                    .get();
            },
            [&](const YaPlugView::OnKeyUp& request)
                -> YaPlugView::OnKeyUp::Response {
                return main_context_
                    .run_in_context([&]() -> tresult {
                        const auto& [instance, _] =
                            get_instance(request.owner_instance_id);

                        return instance.plug_view_instance->plug_view->onKeyUp(
                            request.key, request.key_code, request.modifiers);
                    })
                    .get();
            },
            [&](YaPlugView::GetSize& request) -> YaPlugView::GetSize::Response {
                // Melda plugins will refuse to open dialogs of this function is
                // not run from the GUI thread. Oh and they also deadlock if
                // audio processing gets initialized at the same time as this
                // function, not sure why.
                Steinberg::ViewRect size{};
                const tresult result =
                    do_mutual_recursion_on_gui_thread([&]() -> tresult {
                        const auto& [instance, _] =
                            get_instance(request.owner_instance_id);
                        std::lock_guard lock(instance.get_size_mutex);

                        return instance.plug_view_instance->plug_view->getSize(
                            &size);
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
                return do_mutual_recursion_on_gui_thread([&]() -> tresult {
                    const auto& [instance, _] =
                        get_instance(request.owner_instance_id);

                    const tresult result =
                        instance.plug_view_instance->plug_view->onSize(
                            &request.new_size);

                    // Also resize our wrapper window if the plugin agreed to
                    // the new size
                    // NOTE: MeldaProduction plugins return `kResultFalse` even
                    //       if they accept the resize, so we shouldn't check
                    //       the result here
                    if (instance.editor) {
                        instance.editor->resize(request.new_size.getWidth(),
                                                request.new_size.getHeight());
                    }

                    return result;
                });
            },
            [&](const YaPlugView::OnFocus& request)
                -> YaPlugView::OnFocus::Response {
                return main_context_
                    .run_in_context([&]() -> tresult {
                        const auto& [instance, _] =
                            get_instance(request.owner_instance_id);

                        return instance.plug_view_instance->plug_view->onFocus(
                            request.state);
                    })
                    .get();
            },
            [&](YaPlugView::SetFrame& request)
                -> YaPlugView::SetFrame::Response {
                // This likely doesn't have to be run from the GUI thread, but
                // since 80% of the `IPlugView` functions have to be we'll do it
                // here anyways
                return main_context_
                    .run_in_context([&]() -> tresult {
                        const auto& [instance, _] =
                            get_instance(request.owner_instance_id);

                        // If the host passed a valid `IPlugFrame*`, then We'll
                        // create a proxy object for the `IPlugFrame` object and
                        // pass that to the `setFrame()` function. The lifetime
                        // of this object is tied to that of the actual
                        // `IPlugFrame` object we're passing this proxy to. IF
                        // the host passed a null pointer (which seems to be
                        // common when terminating plugins) we'll do the same
                        // thing here.
                        instance.plug_frame_proxy =
                            request.plug_frame_args
                                ? Steinberg::owned(new Vst3PlugFrameProxyImpl(
                                      *this,
                                      std::move(*request.plug_frame_args)))
                                : nullptr;

                        return instance.plug_view_instance->plug_view->setFrame(
                            instance.plug_frame_proxy);
                    })
                    .get();
            },
            [&](YaPlugView::CanResize& request)
                -> YaPlugView::CanResize::Response {
                // To prevent weird behaviour we'll perform all size related
                // functions from the GUI thread, including this one
                return do_mutual_recursion_on_gui_thread([&]() -> tresult {
                    const auto& [instance, _] =
                        get_instance(request.owner_instance_id);

                    return instance.plug_view_instance->plug_view->canResize();
                });
            },
            [&](YaPlugView::CheckSizeConstraint& request)
                -> YaPlugView::CheckSizeConstraint::Response {
                const tresult result =
                    do_mutual_recursion_on_gui_thread([&]() -> tresult {
                        const auto& [instance, _] =
                            get_instance(request.owner_instance_id);

                        return instance.plug_view_instance->plug_view
                            ->checkSizeConstraint(&request.rect);
                    });

                return YaPlugView::CheckSizeConstraintResponse{
                    .result = result, .updated_rect = std::move(request.rect)};
            },
            [&](YaPlugViewContentScaleSupport::SetContentScaleFactor& request)
                -> YaPlugViewContentScaleSupport::SetContentScaleFactor::
                    Response {
                        if (config_.vst3_no_scaling) {
                            std::cerr << "The host requested the editor GUI to "
                                         "be scaled by a factor of "
                                      << request.factor
                                      << ", but the 'vst3_no_scale' option is "
                                         "enabled. Ignoring the request."
                                      << std::endl;
                            return Steinberg::kNotImplemented;
                        } else {
                            return main_context_
                                .run_in_context([&]() -> tresult {
                                    const auto& [instance, _] =
                                        get_instance(request.owner_instance_id);

                                    return instance.plug_view_instance
                                        ->plug_view_content_scale_support
                                        ->setContentScaleFactor(request.factor);
                                })
                                .get();
                        }
                    },
            [&](Vst3PluginProxy::Initialize& request)
                -> Vst3PluginProxy::Initialize::Response {
                // Since plugins might want to start timers in
                // `IPlugView::{initialize,terminate}`, we'll run these
                // functions from the main GUI thread
                return main_context_
                    .run_in_context([&]()
                                        -> Vst3PluginProxy::InitializeResponse {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        // We'll create a proxy object for the host context
                        // passed by the host and pass that to the initialize
                        // function. The lifetime of this object is tied to that
                        // of the actual plugin object we're proxying for.
                        instance.host_context_proxy =
                            Steinberg::owned(new Vst3HostContextProxyImpl(
                                *this, std::move(request.host_context_args)));

                        // The plugin may try to spawn audio worker threads
                        // during its initialization
                        set_realtime_priority(true);
                        // This static cast is required to upcast to
                        // `FUnknown*`
                        const tresult result =
                            instance.interfaces.plugin_base->initialize(
                                static_cast<YaHostApplication*>(
                                    instance.host_context_proxy));
                        set_realtime_priority(false);

                        // HACK: Waves plugins for some reason only add
                        //       `IEditController` to their query interface
                        //       after `IPluginBase::initialize()` has been
                        //       called, so we need to update the list of
                        //       supported interfaces at this point. This
                        //       needs to be done on both the Wine and the
                        //       plugin since, so we also need to return an
                        //       updated list of supported interfaces.
                        instance.interfaces =
                            Vst3PluginInterfaces(instance.object);

                        Vst3PluginProxy::ConstructArgs updated_interfaces(
                            instance.object, request.instance_id);

                        // The Win32 message loop will not be run up to this
                        // point to prevent plugins with partially
                        // initialized states from misbehaving
                        instance.is_initialized = true;

                        return Vst3PluginProxy::InitializeResponse{
                            .result = result,
                            .updated_plugin_interfaces = updated_interfaces};
                    })
                    .get();
            },
            [&](const YaPluginBase::Terminate& request)
                -> YaPluginBase::Terminate::Response {
                return main_context_
                    .run_in_context([&]() -> tresult {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        // HACK: New (anno May/June 2022) Arturia VST3 plugins
                        //       don't check whether the data they try to access
                        //       from their Win32 timers is actually
                        //       initialized, and this function deinitializes
                        //       that data. So if this is followed by
                        //       `handle_events()`, then the plugin would run
                        //       into a memory error. Inhibiting that event loop
                        //       'fixes' this.
                        instance.is_initialized = false;

                        return instance.interfaces.plugin_base->terminate();
                    })
                    .get();
            },
            [&](const YaProgramListData::ProgramDataSupported& request)
                -> YaProgramListData::ProgramDataSupported::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.program_list_data
                    ->programDataSupported(request.list_id);
            },
            [&](const YaProcessContextRequirements::
                    GetProcessContextRequirements& request)
                -> YaProcessContextRequirements::GetProcessContextRequirements::
                    Response {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        return instance.interfaces.process_context_requirements
                            ->getProcessContextRequirements();
                    },
            [&](YaProgramListData::GetProgramData& request)
                -> YaProgramListData::GetProgramData::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                const tresult result =
                    instance.interfaces.program_list_data->getProgramData(
                        request.list_id, request.program_index, &request.data);

                return YaProgramListData::GetProgramDataResponse{
                    .result = result, .data = std::move(request.data)};
            },
            [&](YaProgramListData::SetProgramData& request)
                -> YaProgramListData::SetProgramData::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.program_list_data->setProgramData(
                    request.list_id, request.program_index, &request.data);
            },
            [&](const YaUnitData::UnitDataSupported& request)
                -> YaUnitData::UnitDataSupported::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.unit_data->unitDataSupported(
                    request.unit_id);
            },
            [&](YaUnitData::GetUnitData& request)
                -> YaUnitData::GetUnitData::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                const tresult result =
                    instance.interfaces.unit_data->getUnitData(request.unit_id,
                                                               &request.data);

                return YaUnitData::GetUnitDataResponse{
                    .result = result, .data = std::move(request.data)};
            },
            [&](YaUnitData::SetUnitData& request)
                -> YaUnitData::SetUnitData::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.unit_data->setUnitData(
                    request.unit_id, &request.data);
            },
            [&](YaPluginFactory3::SetHostContext& request)
                -> YaPluginFactory3::SetHostContext::Response {
                plugin_factory_host_context_ =
                    Steinberg::owned(new Vst3HostContextProxyImpl(
                        *this, std::move(request.host_context_args)));

                Steinberg::FUnknownPtr<Steinberg::IPluginFactory3> factory_3(
                    module_->getFactory().get());
                assert(factory_3);

                // This static cast is required to upcast to `FUnknown*`
                return factory_3->setHostContext(
                    static_cast<YaHostApplication*>(
                        plugin_factory_host_context_));
            },
            [&](const YaUnitInfo::GetUnitCount& request)
                -> YaUnitInfo::GetUnitCount::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.unit_info->getUnitCount();
            },
            [&](const YaUnitInfo::GetUnitInfo& request)
                -> YaUnitInfo::GetUnitInfo::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                Steinberg::Vst::UnitInfo info{};
                const tresult result =
                    instance.interfaces.unit_info->getUnitInfo(
                        request.unit_index, info);

                return YaUnitInfo::GetUnitInfoResponse{.result = result,
                                                       .info = std::move(info)};
            },
            [&](const YaUnitInfo::GetProgramListCount& request)
                -> YaUnitInfo::GetProgramListCount::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.unit_info->getProgramListCount();
            },
            [&](const YaUnitInfo::GetProgramListInfo& request)
                -> YaUnitInfo::GetProgramListInfo::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                Steinberg::Vst::ProgramListInfo info{};
                const tresult result =
                    instance.interfaces.unit_info->getProgramListInfo(
                        request.list_index, info);

                return YaUnitInfo::GetProgramListInfoResponse{
                    .result = result, .info = std::move(info)};
            },
            [&](const YaUnitInfo::GetProgramName& request)
                -> YaUnitInfo::GetProgramName::Response {
                Steinberg::Vst::String128 name{0};
                // NOTE: This will likely be requested in response to
                //       `IUnitHandler::notifyProgramListChange`, but some
                //       plugins (like TEOTE) require this to be called from the
                //       same thread when that happens.
                const tresult result =
                    do_mutual_recursion_on_off_thread([&]() -> tresult {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        return instance.interfaces.unit_info->getProgramName(
                            request.list_id, request.program_index, name);
                    });

                return YaUnitInfo::GetProgramNameResponse{
                    .result = result, .name = tchar_pointer_to_u16string(name)};
            },
            [&](const YaUnitInfo::GetProgramInfo& request)
                -> YaUnitInfo::GetProgramInfo::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                Steinberg::Vst::String128 attribute_value{0};
                const tresult result =
                    instance.interfaces.unit_info->getProgramInfo(
                        request.list_id, request.program_index,
                        request.attribute_id.c_str(), attribute_value);

                return YaUnitInfo::GetProgramInfoResponse{
                    .result = result,
                    .attribute_value =
                        tchar_pointer_to_u16string(attribute_value)};
            },
            [&](const YaUnitInfo::HasProgramPitchNames& request)
                -> YaUnitInfo::HasProgramPitchNames::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.unit_info->hasProgramPitchNames(
                    request.list_id, request.program_index);
            },
            [&](const YaUnitInfo::GetProgramPitchName& request)
                -> YaUnitInfo::GetProgramPitchName::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                Steinberg::Vst::String128 name{0};
                const tresult result =
                    instance.interfaces.unit_info->getProgramPitchName(
                        request.list_id, request.program_index,
                        request.midi_pitch, name);

                return YaUnitInfo::GetProgramPitchNameResponse{
                    .result = result, .name = tchar_pointer_to_u16string(name)};
            },
            [&](const YaUnitInfo::GetSelectedUnit& request)
                -> YaUnitInfo::GetSelectedUnit::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.unit_info->getSelectedUnit();
            },
            [&](const YaUnitInfo::SelectUnit& request)
                -> YaUnitInfo::SelectUnit::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.unit_info->selectUnit(
                    request.unit_id);
            },
            [&](const YaUnitInfo::GetUnitByBus& request)
                -> YaUnitInfo::GetUnitByBus::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                Steinberg::Vst::UnitID unit_id;
                const tresult result =
                    instance.interfaces.unit_info->getUnitByBus(
                        request.type, request.dir, request.bus_index,
                        request.channel, unit_id);

                return YaUnitInfo::GetUnitByBusResponse{.result = result,
                                                        .unit_id = unit_id};
            },
            [&](YaUnitInfo::SetUnitProgramData& request)
                -> YaUnitInfo::SetUnitProgramData::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.unit_info->setUnitProgramData(
                    request.list_or_unit_id, request.program_index,
                    &request.data);
            },
            [&](YaXmlRepresentationController::GetXmlRepresentationStream&
                    request)
                -> YaXmlRepresentationController::GetXmlRepresentationStream::
                    Response {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        const tresult result =
                            instance.interfaces.xml_representation_controller
                                ->getXmlRepresentationStream(request.info,
                                                             &request.stream);

                        return YaXmlRepresentationController::
                            GetXmlRepresentationStreamResponse{
                                .result = result,
                                .stream = std::move(request.stream)};
                    },
        });
}

bool Vst3Bridge::maybe_resize_editor(size_t instance_id,
                                     const Steinberg::ViewRect& new_size) {
    const auto& [instance, _] = get_instance(instance_id);

    if (instance.editor) {
        instance.editor->resize(new_size.getWidth(), new_size.getHeight());
        return true;
    } else {
        return false;
    }
}

void Vst3Bridge::register_context_menu(Vst3ContextMenuProxyImpl& context_menu) {
    const auto& [owner_instance, _] =
        get_instance(context_menu.owner_instance_id());
    std::lock_guard lock(owner_instance.registered_context_menus_mutex);

    owner_instance.registered_context_menus.emplace(
        context_menu.context_menu_id(),
        std::ref<Vst3ContextMenuProxyImpl>(context_menu));
}

void Vst3Bridge::unregister_context_menu(
    Vst3ContextMenuProxyImpl& context_menu) {
    const auto& [owner_instance, _] =
        get_instance(context_menu.owner_instance_id());
    std::lock_guard lock(owner_instance.registered_context_menus_mutex);

    owner_instance.registered_context_menus.erase(
        context_menu.context_menu_id());
}

void Vst3Bridge::close_sockets() {
    sockets_.close();
}

size_t Vst3Bridge::generate_instance_id() noexcept {
    return current_instance_id_.fetch_add(1);
}

std::pair<Vst3PluginInstance&, std::shared_lock<std::shared_mutex>>
Vst3Bridge::get_instance(size_t instance_id) noexcept {
    std::shared_lock lock(object_instances_mutex_);

    return std::pair<Vst3PluginInstance&, std::shared_lock<std::shared_mutex>>(
        object_instances_.at(instance_id), std::move(lock));
}

std::optional<AudioShmBuffer::Config> Vst3Bridge::setup_shared_audio_buffers(
    size_t instance_id) {
    const auto& [instance, _] = get_instance(instance_id);

    const Steinberg::IPtr<Steinberg::Vst::IComponent> component =
        instance.interfaces.component;
    const Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> audio_processor =
        instance.interfaces.audio_processor;

    if (!instance.process_setup || !component || !audio_processor) {
        return std::nullopt;
    }

    // We'll query the plugin for its audio bus layouts, and then create
    // calculate the offsets in a large memory buffer for the different audio
    // channels. The offsets for each audio channel are in samples (since
    // they'll be used with pointer arithmetic in `AudioShmBuffer`).
    uint32_t current_offset = 0;

    auto create_bus_offsets = [&, &setup = instance.process_setup](
                                  Steinberg::Vst::BusDirection direction) {
        const auto num_busses =
            component->getBusCount(Steinberg::Vst::kAudio, direction);

        // This function is also run from `IAudioProcessor::setActive()`.
        // According to the docs this does not need to be realtime-safe, but we
        // should at least still try to not do anything expensive when no work
        // needs to be done.
        llvm::SmallVector<llvm::SmallVector<uint32_t, 32>, 16> bus_offsets(
            num_busses);
        for (int bus = 0; bus < num_busses; bus++) {
            Steinberg::Vst::SpeakerArrangement speaker_arrangement{};
            audio_processor->getBusArrangement(direction, bus,
                                               speaker_arrangement);

            const size_t num_channels =
                std::bitset<sizeof(Steinberg::Vst::SpeakerArrangement) * 8>(
                    speaker_arrangement)
                    .count();
            bus_offsets[bus].resize(num_channels);

            for (size_t channel = 0; channel < num_channels; channel++) {
                bus_offsets[bus][channel] = current_offset;
                current_offset += setup->maxSamplesPerBlock;
            }
        }

        return bus_offsets;
    };

    // Creating the audio buffer offsets for every channel in every bus will
    // advacne `current_offset` to keep pointing to the starting position for
    // the next channel
    const auto input_bus_offsets = create_bus_offsets(Steinberg::Vst::kInput);
    const auto output_bus_offsets = create_bus_offsets(Steinberg::Vst::kOutput);

    // The size of the buffer is in bytes, and it will depend on whether the
    // host is going to pass 32-bit or 64-bit audio to the plugin
    const bool double_precision =
        instance.process_setup->symbolicSampleSize == Steinberg::Vst::kSample64;
    const uint32_t buffer_size =
        current_offset * (double_precision ? sizeof(double) : sizeof(float));

    // If this function has been called previously and the size did not change,
    // then we should not do any work
    if (instance.process_buffers &&
        instance.process_buffers->config_.size == buffer_size) {
        return std::nullopt;
    }

    // Because the above check should be super cheap, we'll now need to convert
    // the stack allocated SmallVectors to regular heap vectors
    std::vector<std::vector<uint32_t>> input_bus_offsets_vector;
    input_bus_offsets_vector.reserve(input_bus_offsets.size());
    for (const auto& channel_offsets : input_bus_offsets) {
        input_bus_offsets_vector.push_back(
            std::vector(channel_offsets.begin(), channel_offsets.end()));
    }

    std::vector<std::vector<uint32_t>> output_bus_offsets_vector;
    output_bus_offsets_vector.reserve(output_bus_offsets.size());
    for (const auto& channel_offsets : output_bus_offsets) {
        output_bus_offsets_vector.push_back(
            std::vector(channel_offsets.begin(), channel_offsets.end()));
    }

    // We'll set up these shared memory buffers on the Wine side first, and then
    // when this request returns we'll do the same thing on the native plugin
    // side
    AudioShmBuffer::Config buffer_config{
        .name = sockets_.base_dir_.filename().string() + "-" +
                std::to_string(instance_id),
        .size = buffer_size,
        .input_offsets = std::move(input_bus_offsets_vector),
        .output_offsets = std::move(output_bus_offsets_vector)};
    if (!instance.process_buffers) {
        instance.process_buffers.emplace(buffer_config);
    } else {
        instance.process_buffers->resize(buffer_config);
    }

    // After setting up the shared memory buffer, we need to create a vector of
    // channel audio pointers for every bus. These will then be assigned to the
    // `AudioBusBuffers` objects in the `ProcessData` struct in
    // `YaProcessData::reconstruct()` before passing the reconstructed process
    // data to `IAudioProcessor::process()`.
    auto set_bus_pointers =
        [&]<std::invocable<uint32_t, uint32_t> F>(
            std::vector<std::vector<void*>>& bus_pointers,
            const std::vector<std::vector<uint32_t>>& bus_offsets,
            F&& get_channel_pointer) {
            bus_pointers.resize(bus_offsets.size());

            for (size_t bus = 0; bus < bus_offsets.size(); bus++) {
                bus_pointers[bus].resize(bus_offsets[bus].size());

                for (size_t channel = 0; channel < bus_offsets[bus].size();
                     channel++) {
                    bus_pointers[bus][channel] =
                        get_channel_pointer(bus, channel);
                }
            }
        };

    set_bus_pointers(
        instance.process_buffers_input_pointers,
        instance.process_buffers->config_.input_offsets,
        [&, &instance = instance](uint32_t bus, uint32_t channel) -> void* {
            if (double_precision) {
                return instance.process_buffers->input_channel_ptr<double>(
                    bus, channel);
            } else {
                return instance.process_buffers->input_channel_ptr<float>(
                    bus, channel);
            }
        });
    set_bus_pointers(
        instance.process_buffers_output_pointers,
        instance.process_buffers->config_.output_offsets,
        [&, &instance = instance](uint32_t bus, uint32_t channel) -> void* {
            if (double_precision) {
                return instance.process_buffers->output_channel_ptr<double>(
                    bus, channel);
            } else {
                return instance.process_buffers->output_channel_ptr<float>(
                    bus, channel);
            }
        });

    return buffer_config;
}

size_t Vst3Bridge::register_object_instance(
    Steinberg::IPtr<Steinberg::FUnknown> object) {
    std::unique_lock lock(object_instances_mutex_);

    const size_t instance_id = generate_instance_id();
    object_instances_.emplace(instance_id, std::move(object));

    // If the object supports `IComponent` or `IAudioProcessor`,
    // then we'll set up a dedicated thread for function calls for
    // those interfaces.
    if (object_instances_.at(instance_id).interfaces.audio_processor ||
        object_instances_.at(instance_id).interfaces.component) {
        std::promise<void> socket_listening_latch;

        object_instances_.at(instance_id)
            .audio_processor_handler = Win32Thread([&, instance_id]() {
            set_realtime_priority(true);

            // XXX: Like with VST2 worker threads, when using plugin groups the
            //      thread names from different plugins will clash. Not a huge
            //      deal probably, since duplicate thread names are still more
            //      useful than no thread names.
            const std::string thread_name =
                "audio-" + std::to_string(instance_id);
            pthread_setname_np(pthread_self(), thread_name.c_str());

            sockets_.add_audio_processor_and_listen(
                instance_id, socket_listening_latch,
                overload{
                    [&](YaAudioProcessor::SetBusArrangements& request)
                        -> YaAudioProcessor::SetBusArrangements::Response {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        // HACK: WA Production Imperfect VST3 somehow requires
                        //       `inputs` to be a valid pointer, even if there
                        //       are no inputs.
                        Steinberg::Vst::SpeakerArrangement empty_arrangement =
                            0b00000000;

                        return instance.interfaces.audio_processor
                            ->setBusArrangements(
                                request.num_ins > 0 ? request.inputs.data()
                                                    : &empty_arrangement,
                                request.num_ins,
                                request.num_outs > 0 ? request.outputs.data()
                                                     : &empty_arrangement,
                                request.num_outs);
                    },
                    [&](YaAudioProcessor::GetBusArrangement& request)
                        -> YaAudioProcessor::GetBusArrangement::Response {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        Steinberg::Vst::SpeakerArrangement arr{};
                        const tresult result =
                            instance.interfaces.audio_processor
                                ->getBusArrangement(request.dir, request.index,
                                                    arr);

                        return YaAudioProcessor::GetBusArrangementResponse{
                            .result = result, .arr = arr};
                    },
                    [&](const YaAudioProcessor::CanProcessSampleSize& request)
                        -> YaAudioProcessor::CanProcessSampleSize::Response {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        return instance.interfaces.audio_processor
                            ->canProcessSampleSize(
                                request.symbolic_sample_size);
                    },
                    [&](const YaAudioProcessor::GetLatencySamples& request)
                        -> YaAudioProcessor::GetLatencySamples::Response {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        return instance.interfaces.audio_processor
                            ->getLatencySamples();
                    },
                    [&](YaAudioProcessor::SetupProcessing& request)
                        -> YaAudioProcessor::SetupProcessing::Response {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        // We'll set up the shared audio buffers on the Wine
                        // side after the plugin has finished doing their setup.
                        // This configuration can then be used on the native
                        // plugin side to connect to the same shared audio
                        // buffers.
                        instance.process_setup = request.setup;

                        return instance.interfaces.audio_processor
                            ->setupProcessing(request.setup);
                    },
                    [&](const YaAudioProcessor::SetProcessing& request)
                        -> YaAudioProcessor::SetProcessing::Response {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);
                        // HACK: MeldaProduction plugins for some reason cannot
                        //       handle it if this function is called from the
                        //       audio thread while at the same time
                        //       `IPlugView::getSize()` is being called from the
                        //       GUI thread
                        std::lock_guard lock(instance.get_size_mutex);

                        return instance.interfaces.audio_processor
                            ->setProcessing(request.state);
                    },
                    [&](MessageReference<YaAudioProcessor::Process>&
                            request_ref)
                        -> YaAudioProcessor::Process::Response {
                        // NOTE: To prevent allocations we keep this actual
                        //       `YaAudioProcessor::Process` object around as
                        //       part of a static thread local
                        //       `AudioProcessorRequest` object, and we only
                        //       store a reference to it in our variant (this is
                        //       done during the deserialization in
                        //       `bitsery::ext::MessageReference`)
                        YaAudioProcessor::Process& request = request_ref.get();

                        // As suggested by Jack Winter, we'll synchronize this
                        // thread's audio processing priority with that of the
                        // host's audio thread every once in a while
                        if (request.new_realtime_priority) {
                            set_realtime_priority(
                                true, *request.new_realtime_priority);
                        }

                        const auto& [instance, _] =
                            get_instance(request.instance_id);
                        // Most plugins will already enable FTZ, but there are a
                        // handful of plugins that don't that suffer from
                        // extreme DSP load increases when they start producing
                        // denormals
                        ScopedFlushToZero ftz_guard;

                        // The actual audio is stored in the shared memory
                        // buffers, so the reconstruction function will need to
                        // know where it should point the `AudioBusBuffers` to
                        // HACK: IK-Multimedia's T-RackS 5 will hang if audio
                        //       processing is done from the audio thread while
                        //       the plugin is in offline processing mode. Yes
                        //       that's as silly as it sounds.
                        tresult result;
                        auto& reconstructed = request.data.reconstruct(
                            instance.process_buffers_input_pointers,
                            instance.process_buffers_output_pointers);
                        if (instance.process_setup &&
                            instance.process_setup->processMode ==
                                Steinberg::Vst::kOffline) {
                            result = main_context_
                                         .run_in_context([&instance = instance,
                                                          &reconstructed]() {
                                             return instance.interfaces
                                                 .audio_processor->process(
                                                     reconstructed);
                                         })
                                         .get();
                        } else {
                            result =
                                instance.interfaces.audio_processor->process(
                                    reconstructed);
                        }

                        return YaAudioProcessor::ProcessResponse{
                            .result = result,
                            .output_data = request.data.create_response()};
                    },
                    [&](const YaAudioProcessor::GetTailSamples& request)
                        -> YaAudioProcessor::GetTailSamples::Response {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        return instance.interfaces.audio_processor
                            ->getTailSamples();
                    },
                    [&](const YaComponent::GetControllerClassId& request)
                        -> YaComponent::GetControllerClassId::Response {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        Steinberg::TUID cid{0};
                        const tresult result =
                            instance.interfaces.component->getControllerClassId(
                                cid);

                        return YaComponent::GetControllerClassIdResponse{
                            .result = result, .editor_cid = cid};
                    },
                    [&](const YaComponent::SetIoMode& request)
                        -> YaComponent::SetIoMode::Response {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        return instance.interfaces.component->setIoMode(
                            request.mode);
                    },
                    [&](const YaComponent::GetBusCount& request)
                        -> YaComponent::GetBusCount::Response {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        return instance.interfaces.component->getBusCount(
                            request.type, request.dir);
                    },
                    [&](YaComponent::GetBusInfo& request)
                        -> YaComponent::GetBusInfo::Response {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        Steinberg::Vst::BusInfo bus{};
                        const tresult result =
                            instance.interfaces.component->getBusInfo(
                                request.type, request.dir, request.index, bus);

                        return YaComponent::GetBusInfoResponse{
                            .result = result, .bus = std::move(bus)};
                    },
                    [&](YaComponent::GetRoutingInfo& request)
                        -> YaComponent::GetRoutingInfo::Response {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        Steinberg::Vst::RoutingInfo out_info{};
                        const tresult result =
                            instance.interfaces.component->getRoutingInfo(
                                request.in_info, out_info);

                        return YaComponent::GetRoutingInfoResponse{
                            .result = result, .out_info = std::move(out_info)};
                    },
                    [&](const YaComponent::ActivateBus& request)
                        -> YaComponent::ActivateBus::Response {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        return instance.interfaces.component->activateBus(
                            request.type, request.dir, request.index,
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
                        return do_mutual_recursion_on_off_thread(
                            [&]() -> YaComponent::SetActive::Response {
                                const auto& [instance, _] =
                                    get_instance(request.instance_id);

                                const tresult result =
                                    instance.interfaces.component->setActive(
                                        request.state);

                                // NOTE: REAPER may change the bus layout after
                                //       calling
                                //       `IAudioProcessor::setupProcessing()`,
                                //       so this place is the only safe place to
                                //       setup the buffers
                                const std::optional<AudioShmBuffer::Config>
                                    updated_audio_buffers_config =
                                        setup_shared_audio_buffers(
                                            request.instance_id);

                                return YaComponent::SetActiveResponse{
                                    .result = result,
                                    .updated_audio_buffers_config = std::move(
                                        updated_audio_buffers_config)};
                            });
                    },
                    [&](const YaPrefetchableSupport::GetPrefetchableSupport&
                            request)
                        -> YaPrefetchableSupport::GetPrefetchableSupport::
                            Response {
                                Steinberg::Vst::PrefetchableSupport
                                    prefetchable;
                                const auto& [instance, _] =
                                    get_instance(request.instance_id);

                                const tresult result =
                                    instance.interfaces.prefetchable_support
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
    if (const auto& [instance, _] = get_instance(instance_id);
        instance.interfaces.audio_processor || instance.interfaces.component) {
        sockets_.remove_audio_processor(instance_id);
    }

    // Remove the instance from within the main IO context so
    // removing it doesn't interfere with the Win32 message loop
    // XXX: I don't think we have to wait for the object to be
    //      deleted most of the time, but I can imagine a situation
    //      where the plugin does a host callback triggered by a
    //      Win32 timer in between where the above closure is being
    //      executed and when the actual host application context on
    //      the plugin side gets deallocated.
    main_context_
        .run_in_context([&, instance_id]() -> void {
            std::unique_lock lock(object_instances_mutex_);
            object_instances_.erase(instance_id);
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
