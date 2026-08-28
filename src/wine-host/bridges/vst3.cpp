// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2026 Robbert van der Helm
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
#include <cstring>

#include "vst3-impls/component-handler-proxy.h"
#include "vst3-impls/connection-point-proxy.h"
#include "vst3-impls/context-menu-proxy.h"
#include "vst3-impls/host-context-proxy.h"
#include "vst3-impls/plug-frame-proxy.h"

// Generated inside of the build directory
#include <version.h>

// NOLINTNEXTLINE(bugprone-suspicious-include)
#include <public.sdk/source/vst/hosting/module_win32.cpp>

#ifdef WITH_ARA
// Wine's headers don't expose SetThreadStackGuarantee, so declare it here.
// The function is present in kernel32.dll on Wine 7+.
extern "C" WINBASEAPI BOOL WINAPI
SetThreadStackGuarantee(ULONG* StackSizeInBytes);
#endif

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
      xml_representation_controller(object)
#ifdef WITH_ARA
      ,
      plug_in_entry_point(object),
      plug_in_entry_point_2(object)
#endif
{
}

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
    config_ = sockets_.plugin_host_callback_.send_message(
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

    // Increase the stack guarantee so Wine can commit more stack pages when
    // C++ exception unwinding in ARA plugins (e.g. JUCE-based) needs extra
    // space. SetThreadStackGuarantee adds committed stack below the current
    // guard page; 1 MB gives enough headroom for the CFA unwind loops.
#ifdef WITH_ARA
    {
        ULONG stack_guarantee = 1 * 1024 * 1024;
        SetThreadStackGuarantee(&stack_guarantee);
    }
#endif

#ifdef WITH_ARA
    const auto resolve_dc =
        [&](native_size_t ara_dc_id) -> AraDocumentControllerInstance* {
        std::lock_guard lock(ara_document_controllers_mutex_);
        auto it = ara_document_controllers_.find(ara_dc_id);
        if (it == ara_document_controllers_.end())
            return nullptr;
        return it->second.get();
    };

    // All ARA document controller interface calls must run on the GUI thread
    // (the same thread that called createDocumentControllerWithDocument).
    // This helper dispatches fn(iface, dcr) on the GUI thread and returns
    // the result. If we're already on the GUI thread (e.g. called from within
    // an attached() or similar callback), execute directly to avoid deadlock.
    const auto dc_call = [&]<typename F>(AraDocumentControllerInstance* dc,
                                         F&& fn) {
        using R = std::invoke_result_t<
            F,
            ARA::ARADocumentControllerInterface*,
            ARA::ARADocumentControllerRef>;
        if (main_context_.is_gui_thread()) {
            return fn(dc->dc_instance->documentControllerInterface,
                      dc->dc_instance->documentControllerRef);
        }
        return main_context_
            .run_in_context([dc, fn = std::forward<F>(fn)]() mutable -> R {
                return fn(dc->dc_instance->documentControllerInterface,
                          dc->dc_instance->documentControllerRef);
            })
            .get();
    };
#endif
    sockets_.host_plugin_control_.receive_messages(
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
                Vst3PluginProxy::ConstructArgs args(instance.object,
                                                    instance_id);

#ifdef WITH_ARA
                if (instance.interfaces.plug_in_entry_point_2 ||
                    instance.interfaces.plug_in_entry_point) {
                    logger_.log(
                        std::string("[ARA] plugin instance ") +
                        std::to_string(instance_id) + " supports ARA" +
                        (instance.interfaces.plug_in_entry_point_2
                             ? "2 (IPlugInEntryPoint2)"
                             : "1 (IPlugInEntryPoint only)"));
                }
#endif

                return args;
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
                    request)
                -> YaAudioPresentationLatency::
                    SetAudioPresentationLatencySamples::Response {
                        return main_context_
                            .run_in_context([&]() -> tresult {
                                const auto& [instance, _] =
                                    get_instance(request.instance_id);

                                return instance.interfaces
                                    .audio_presentation_latency
                                    ->setAudioPresentationLatencySamples(
                                        request.dir, request.bus_index,
                                        request.latency_in_samples);
                            })
                            .get();
                    },
            [&](YaAutomationState::SetAutomationState& request)
                -> YaAutomationState::SetAutomationState::Response {
                return main_context_
                    .run_in_context([&]() -> tresult {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        return instance.interfaces.automation_state
                            ->setAutomationState(request.state);
                    })
                    .get();
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
                return main_context_
                    .run_in_context([&]() -> tresult {
                        return std::visit(
                            overload{
                                [&](const native_size_t& other_instance_id)
                                    -> tresult {
                                    const auto& [this_instance, _] =
                                        get_instance(request.instance_id);
                                    const auto& [other_instance, _2] =
                                        get_instance(other_instance_id);

                                    return this_instance.interfaces
                                        .connection_point->connect(
                                            other_instance.interfaces
                                                .connection_point);
                                },
                                [&](Vst3ConnectionPointProxy::ConstructArgs&
                                        args) -> tresult {
                                    const auto& [this_instance, _] =
                                        get_instance(request.instance_id);

                                    this_instance.connection_point_proxy =
                                        Steinberg::owned(
                                            new Vst3ConnectionPointProxyImpl(
                                                *this, std::move(args)));

                                    return this_instance.interfaces
                                        .connection_point->connect(
                                            this_instance
                                                .connection_point_proxy);
                                }},
                            request.other);
                    })
                    .get();
            },
            [&](const YaConnectionPoint::Disconnect& request)
                -> YaConnectionPoint::Disconnect::Response {
                return main_context_
                    .run_in_context([&]() -> tresult {
                        const auto& [this_instance, _] =
                            get_instance(request.instance_id);

                        // If the objects were connected directly we can also
                        // disconnect them directly. Otherwise we'll disconnect
                        // them from our proxy object and then destroy that
                        // proxy object.
                        if (request.other_instance_id) {
                            const auto& [other_instance, _2] =
                                get_instance(*request.other_instance_id);

                            return this_instance.interfaces.connection_point
                                ->disconnect(
                                    other_instance.interfaces.connection_point);
                        } else {
                            const tresult result =
                                this_instance.interfaces.connection_point
                                    ->disconnect(
                                        this_instance.connection_point_proxy);
                            this_instance.connection_point_proxy.reset();

                            return result;
                        }
                    })
                    .get();
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
                return main_context_
                    .run_in_context([&]() -> tresult {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        return instance.interfaces.edit_controller
                            ->setComponentState(&request.state);
                    })
                    .get();
            },
            [&](const YaEditController::GetParameterInfos& request)
                -> YaEditController::GetParameterInfos::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                // This is an optimization mostly for Kontakt, which may tell
                // tell the host to rescan its 3000 parameters hundreds of times
                // in rapid succession. Querying all parameters at once can save
                // minutes of waiting around on slower machines.
                const int num_parameters =
                    instance.interfaces.edit_controller->getParameterCount();

                std::vector<std::optional<Steinberg::Vst::ParameterInfo>> infos;
                infos.reserve(num_parameters);
                for (int i = 0; i < num_parameters; i++) {
                    // This should never fail, but we can't make things up and
                    // we don't want to change parameter orders around so we'll
                    // store a nullopt if the plugin returns an error here
                    Steinberg::Vst::ParameterInfo info{};
                    if (instance.interfaces.edit_controller->getParameterInfo(
                            i, info) == Steinberg::kResultOk) {
                        infos.push_back(std::move(info));
                    } else {
                        infos.push_back(std::nullopt);
                    }
                }

                return YaEditController::GetParameterInfosResponse{
                    .infos = std::move(infos)};
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
                return main_context_
                    .run_in_context([&]() -> tresult {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        // If the host passed a valid component handler, then
                        // we'll create a proxy object for the component handler
                        // and pass that to the initialize function. The
                        // lifetime of this object is tied to that of the actual
                        // plugin object we're proxying for. Otherwise we'll
                        // also pass a null pointer. This often happens just
                        // before the host terminates the plugin.
                        instance.component_handler_proxy =
                            request.component_handler_proxy_args
                                ? Steinberg::owned(
                                      new Vst3ComponentHandlerProxyImpl(
                                          *this,
                                          std::move(
                                              *request
                                                   .component_handler_proxy_args)))
                                : nullptr;

                        return instance.interfaces.edit_controller
                            ->setComponentHandler(
                                instance.component_handler_proxy);
                    })
                    .get();
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
                // We'll ignore the UI thread requirement for the parameter
                // functions
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.edit_controller_2->setKnobMode(
                    request.mode);
            },
            [&](const YaEditController2::OpenHelp& request)
                -> YaEditController2::OpenHelp::Response {
                return main_context_
                    .run_in_context([&]() -> tresult {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        return instance.interfaces.edit_controller_2->openHelp(
                            request.only_check);
                    })
                    .get();
            },
            [&](const YaEditController2::OpenAboutBox& request)
                -> YaEditController2::OpenAboutBox::Response {
                return main_context_
                    .run_in_context([&]() -> tresult {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        return instance.interfaces.edit_controller_2
                            ->openAboutBox(request.only_check);
                    })
                    .get();
            },
            [&](const YaEditControllerHostEditing::BeginEditFromHost& request)
                -> YaEditControllerHostEditing::BeginEditFromHost::Response {
                // We'll ignore the UI thread requirement for the parameter
                // functions
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.edit_controller_host_editing
                    ->beginEditFromHost(request.param_id);
            },
            [&](const YaEditControllerHostEditing::EndEditFromHost& request)
                -> YaEditControllerHostEditing::EndEditFromHost::Response {
                // We'll ignore the UI thread requirement for the parameter
                // functions
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
                return main_context_
                    .run_in_context([&]() -> tresult {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        return instance.interfaces.midi_learn
                            ->onLiveMIDIControllerInput(request.bus_index,
                                                        request.channel,
                                                        request.midi_cc);
                    })
                    .get();
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
                        Steinberg::ViewRect size;
                        std::optional<Size> initial_size;
                        // Only accept the initial size from the plugin if it's
                        // valid. Some plugins like Spectrasonics' Omnisphere 2
                        // return 0x0 for the initial size, which breaks our
                        // reparenting in the editor.
                        if (instance.plug_view_instance->plug_view->getSize(
                                &size) == Steinberg::kResultOk &&
                            size.getWidth() > 0 && size.getHeight() > 0) {
                            initial_size.emplace(size.getWidth(),
                                                 size.getHeight());
                        }

                        // HACK: Create a resize watchdog that periodically
                        // verifies the wrapper window size matches the expected
                        // size. This works around VST3 resize issues (mostly)
                        // in Ardour during mutual recursion where X11
                        // operations may not be applied and the wrapper window
                        // remains smaller or larger than the wine window. The
                        // goal here is eventual consistency
                        auto resize_watchdog = [&instance = instance] {
                            if (instance.editor) {
                                if (const auto expected =
                                        instance.editor
                                            ->check_size_mismatch()) {
                                    // Resize the plugin view to propagate the
                                    // target size everywhere.
                                    if (instance.plug_view_instance) {
                                        Steinberg::ViewRect rect{
                                            0, 0,
                                            (expected->width),
                                            (expected->height)};
                                        instance.plug_frame_proxy->resizeView(
                                            instance.plug_view_instance
                                                ->plug_view,
                                            &rect);
                                    }
                                }
                            }
                        };

                        Editor& editor_instance = instance.editor.emplace(
                            main_context_, config_, generic_logger_, x11_handle,
                            std::move(resize_watchdog), initial_size);
                        const tresult result =
                            instance.plug_view_instance->plug_view->attached(
                                editor_instance.win32_handle(), type.c_str());

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

#ifdef WITH_ARA
                            {
                            std::lock_guard sel_lock(instance.last_ara_selection_mutex);
                            if (instance.last_ara_selection &&
                                instance.ara_extension_instance.load(
                                    std::memory_order_relaxed)) {
                                const auto* ext =
                                    instance.ara_extension_instance.load(
                                        std::memory_order_relaxed);
                                if (ext->editorViewInterface &&
                                    ext->editorViewInterface->notifySelection) {
                                    const auto& sel =
                                        *instance.last_ara_selection;
                                    std::vector<ARA::ARAPlaybackRegionRef>
                                        regions;
                                    regions.reserve(
                                        sel.playback_region_refs.size());
                                    for (auto h : sel.playback_region_refs)
                                        regions.push_back(
                                            reinterpret_cast<
                                                ARA::ARAPlaybackRegionRef>(h));
                                    std::vector<ARA::ARARegionSequenceRef> seqs;
                                    seqs.reserve(
                                        sel.region_sequence_refs.size());
                                    for (auto h : sel.region_sequence_refs)
                                        seqs.push_back(
                                            reinterpret_cast<
                                                ARA::ARARegionSequenceRef>(h));
                                    ARA::ARAContentTimeRange time_range_s{};
                                    ARA::ARAViewSelection view_sel{};
                                    view_sel.structSize =
                                        ARA_IMPLEMENTED_STRUCT_SIZE(
                                            ARAViewSelection, timeRange);
                                    view_sel.playbackRegionRefsCount =
                                        static_cast<ARA::ARASize>(
                                            regions.size());
                                    view_sel.playbackRegionRefs =
                                        regions.empty() ? nullptr
                                                        : regions.data();
                                    view_sel.regionSequenceRefsCount =
                                        static_cast<ARA::ARASize>(seqs.size());
                                    view_sel.regionSequenceRefs =
                                        seqs.empty() ? nullptr : seqs.data();
                                    if (sel.time_range) {
                                        time_range_s = {sel.time_range->start,
                                                        sel.time_range->duration};
                                        view_sel.timeRange = &time_range_s;
                                    }
                                    ext->editorViewInterface->notifySelection(
                                        reinterpret_cast<ARA::ARAEditorViewRef>(
                                            sel.editor_view_ref),
                                        &view_sel);
                                }
                            }
                            } // last_ara_selection_mutex scope
#endif
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

                        auto result =
                            instance.plug_view_instance->plug_view->getSize(
                                &size);
                        // HACK: Sometimes, due to HiDPI scaling, plugins might
                        //       end up with a size that is off by one pixel
                        //       from the requested size. To avoid ending up in
                        //       an infinite loop, just return the size that the
                        //       host requested in this case.
                        if (result == Steinberg::kResultOk &&
                            abs(size.getWidth() -
                                instance.last_set_size.getWidth()) <= 1 &&
                            abs(size.getHeight() -
                                instance.last_set_size.getHeight()) <= 1) {
                            size = instance.last_set_size;
                        }
                        return result;
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

                    instance.last_set_size = request.new_size;

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
                        if (config_.editor_disable_host_scaling) {
                            std::cerr
                                << "The host requested the editor GUI to be "
                                   "scaled by a factor of "
                                << request.factor
                                << ", but the 'editor_disable_host_scaling' "
                                   "option is enabled. Ignoring the request."
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
                return main_context_
                    .run_in_context(
                        [&]() -> YaProgramListData::GetProgramDataResponse {
                            const auto& [instance, _] =
                                get_instance(request.instance_id);

                            const tresult result =
                                instance.interfaces.program_list_data
                                    ->getProgramData(request.list_id,
                                                     request.program_index,
                                                     &request.data);

                            return YaProgramListData::GetProgramDataResponse{
                                .result = result,
                                .data = std::move(request.data)};
                        })
                    .get();
            },
            [&](YaProgramListData::SetProgramData& request)
                -> YaProgramListData::SetProgramData::Response {
                return main_context_
                    .run_in_context([&]() -> tresult {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        return instance.interfaces.program_list_data
                            ->setProgramData(request.list_id,
                                             request.program_index,
                                             &request.data);
                    })
                    .get();
            },
            [&](const YaUnitData::UnitDataSupported& request)
                -> YaUnitData::UnitDataSupported::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return instance.interfaces.unit_data->unitDataSupported(
                    request.unit_id);
            },
            [&](YaUnitData::GetUnitData& request)
                -> YaUnitData::GetUnitData::Response {
                return main_context_
                    .run_in_context([&]() -> YaUnitData::GetUnitDataResponse {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        const tresult result =
                            instance.interfaces.unit_data->getUnitData(
                                request.unit_id, &request.data);

                        return YaUnitData::GetUnitDataResponse{
                            .result = result, .data = std::move(request.data)};
                    })
                    .get();
            },
            [&](YaUnitData::SetUnitData& request)
                -> YaUnitData::SetUnitData::Response {
                return main_context_
                    .run_in_context([&]() -> tresult {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        return instance.interfaces.unit_data->setUnitData(
                            request.unit_id, &request.data);
                    })
                    .get();
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
                return main_context_
                    .run_in_context([&]() -> tresult {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);

                        return instance.interfaces.unit_info->selectUnit(
                            request.unit_id);
                    })
                    .get();
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
                        return main_context_
                            .run_in_context(
                                [&]()
                                    -> YaXmlRepresentationController::
                                        GetXmlRepresentationStreamResponse {
                                            const auto& [instance, _] =
                                                get_instance(
                                                    request.instance_id);

                                            const tresult result =
                                                instance.interfaces
                                                    .xml_representation_controller
                                                    ->getXmlRepresentationStream(
                                                        request.info,
                                                        &request.stream);

                                            return YaXmlRepresentationController::
                                                GetXmlRepresentationStreamResponse{
                                                    .result = result,
                                                    .stream = std::move(
                                                        request.stream)};
                                        })
                            .get();
                    },
#ifdef WITH_ARA
        [&](YaPlugInEntryPoint::GetFactory& request)
            -> YaPlugInEntryPoint::GetFactory::Response {
            const auto& [instance, _] = get_instance(request.instance_id);
            // IPlugInEntryPoint exposes getFactory(); IPlugInEntryPoint2 does not.
            // For IPlugInEntryPoint2-only plugins, fall back to scanning the
            // module factory for a matching IMainFactory class.
            const ARA::ARAFactory* factory = nullptr;
            if (instance.interfaces.plug_in_entry_point) {
                factory = instance.interfaces.plug_in_entry_point->getFactory();
            } else if (instance.interfaces.plug_in_entry_point_2) {
                Steinberg::IPtr<Steinberg::IPluginFactory> plug_factory(
                    module_->getFactory().get());
                if (plug_factory) {
                    const int32_t count = plug_factory->countClasses();
                    for (int32_t i = 0; i < count && !factory; ++i) {
                        Steinberg::PClassInfo ci{};
                        if (plug_factory->getClassInfo(i, &ci) !=
                            Steinberg::kResultOk)
                            continue;
                        if (strcmp(ci.category, kARAMainFactoryClass) != 0)
                            continue;
                        ARA::IMainFactory* mf = nullptr;
                        plug_factory->createInstance(
                            ci.cid, ARA::IMainFactory::iid.toTUID(),
                            reinterpret_cast<void**>(&mf));
                        if (mf) {
                            factory = mf->getFactory();
                            mf->release();
                        }
                    }
                }
            }
            if (factory) {
                return from_ara_factory(factory);
            }
            return UniversalTResult(Steinberg::kResultFalse);
        },
        [&](YaPlugInEntryPoint::BindToDocumentControllerWithRoles& request)
            -> YaPlugInEntryPoint::BindToDocumentControllerWithRoles::Response {
            // ara_dc_id is our internal map key; look up the actual plugin-side
            // document controller ref from the map.
            ARA::ARADocumentControllerRef dc_ref = nullptr;
            {
                auto* dc = resolve_dc(request.ara_dc_id);
                if (dc)
                    dc_ref = dc->dc_ref;
            }
            if (!dc_ref) {
                return UniversalTResult(Steinberg::kResultFalse);
            }

            // bindToDocumentControllerWithRoles must run on the GUI thread.
            // Melodyne initialises COM STA apartments during this call and
            // needs the Win32 message pump to be live for cross-apartment
            // marshalling.  Using CreateThread + WaitForSingleObject blocks
            // the GUI thread's message pump and produces a critical-section
            // deadlock.  run_in_context dispatches to the asio GUI thread
            // where the message loop runs, matching every other plugin call.
            const ARA::ARAPlugInExtensionInstance* ext =
                main_context_
                    .run_in_context([&]() -> const ARA::ARAPlugInExtensionInstance* {
                        const auto& [instance, _] =
                            get_instance(request.instance_id);
                        if (instance.interfaces.plug_in_entry_point_2) {
                            return instance.interfaces.plug_in_entry_point_2
                                ->bindToDocumentControllerWithRoles(
                                    dc_ref,
                                    static_cast<ARA::ARAPlugInInstanceRoleFlags>(
                                        request.known_roles),
                                    static_cast<ARA::ARAPlugInInstanceRoleFlags>(
                                        request.assigned_roles));
                        } else if (instance.interfaces.plug_in_entry_point &&
                                   request.known_roles ==
                                       YaPlugInEntryPoint::kARALegacyRoles &&
                                   request.assigned_roles ==
                                       YaPlugInEntryPoint::kARALegacyRoles) {
                            return instance.interfaces.plug_in_entry_point
                                ->bindToDocumentController(dc_ref);
                        }
                        std::string unavailable_interface;
                        if (!instance.interfaces.plug_in_entry_point_2 &&
                            !instance.interfaces.plug_in_entry_point) {
                            unavailable_interface = "IPlugInEntryPoint2 and IPlugInEntryPoint";
                        } else if (!instance.interfaces.plug_in_entry_point_2) {
                            unavailable_interface = "IPlugInEntryPoint2";
                        } else {
                            unavailable_interface = "IPlugInEntryPoint (with legacy roles)";
                        }
                        logger_.log("WARNING: bindToDocumentControllerWithRoles failed for instance " +
                                   std::to_string(request.instance_id) +
                                   " - " + unavailable_interface + " unavailable");
                        return nullptr;
                    })
                    .get();

            if (!ext) {
                return UniversalTResult(Steinberg::kResultFalse);
            }

            main_context_
                .run_in_context([&]() {
                    auto [instance, _] = get_instance(request.instance_id);
                    instance.ara_extension_instance.store(
                        ext, std::memory_order_release);
                })
                .get();

            return YaAraPlugInExtensionInstance{
                .has_playback_renderer =
                    ext->playbackRendererInterface != nullptr,
                .has_editor_renderer = ext->editorRendererInterface != nullptr,
                .has_editor_view = ext->editorViewInterface != nullptr,
                .playback_renderer_ref = reinterpret_cast<uint64_t>(
                    ext->playbackRendererRef),
                .editor_renderer_ref = reinterpret_cast<uint64_t>(
                    ext->editorRendererRef),
                .editor_view_ref = reinterpret_cast<uint64_t>(
                    ext->editorViewRef)};
        },
        [&](const YaMainFactory::Construct& request)
            -> YaMainFactory::Construct::Response {
            Steinberg::TUID cid;
            ArrayUID wine_cid = request.cid.get_wine_uid();
            std::copy(wine_cid.begin(), wine_cid.end(), cid);

            Steinberg::FUnknownPtr<Steinberg::IPluginFactory> factory(
                module_->getFactory().get());
            if (!factory) {
                return UniversalTResult(Steinberg::kResultFalse);
            }

            ARA::IMainFactory* main_factory = nullptr;
            factory->createInstance(cid, ARA::IMainFactory::iid.toTUID(),
                                    reinterpret_cast<void**>(&main_factory));
            if (!main_factory) {
                return UniversalTResult(Steinberg::kResultFalse);
            }

            const ARA::ARAFactory* ara_factory = main_factory->getFactory();

            if (!ara_factory) {
                main_factory->release();
                return UniversalTResult(Steinberg::kResultFalse);
            }

            auto result = from_ara_factory(ara_factory);
            main_factory->release();
            return result;
        },
        [&](const YaAra::CreateDocumentController& request)
            -> YaAra::CreateDocumentController::Response {
            const ARA::ARAFactory* ara_factory = nullptr;
            if (request.instance_id != 0) {
                const auto& [instance, _] =
                    get_instance(request.instance_id);
                if (instance.interfaces.plug_in_entry_point) {
                    ara_factory =
                        instance.interfaces.plug_in_entry_point->getFactory();
                } else if (instance.interfaces.plug_in_entry_point_2) {
                    // IPlugInEntryPoint2 has no getFactory(); scan for a
                    // matching IMainFactory class in the module factory.
                    Steinberg::IPtr<Steinberg::IPluginFactory> plug_factory(
                        module_->getFactory().get());
                    if (plug_factory) {
                        const int32_t count = plug_factory->countClasses();
                        for (int32_t i = 0; i < count && !ara_factory; ++i) {
                            Steinberg::PClassInfo ci{};
                            if (plug_factory->getClassInfo(i, &ci) !=
                                Steinberg::kResultOk)
                                continue;
                            if (strcmp(ci.category, kARAMainFactoryClass) != 0)
                                continue;
                            auto cached =
                                ara_main_factories_.find(request.factory_id);
                            if (cached != ara_main_factories_.end()) {
                                ara_factory = cached->second->getFactory();
                                break;
                            }
                            ARA::IMainFactory* mf = nullptr;
                            plug_factory->createInstance(
                                ci.cid, ARA::IMainFactory::iid.toTUID(),
                                reinterpret_cast<void**>(&mf));
                            if (mf) {
                                const ARA::ARAFactory* f = mf->getFactory();
                                if (f &&
                                    request.factory_id == f->factoryID) {
                                    ara_main_factories_.emplace(
                                        request.factory_id,
                                        Steinberg::IPtr<ARA::IMainFactory>(
                                            mf, false));
                                    ara_factory = f;
                                } else {
                                    mf->release();
                                }
                            }
                        }
                    }
                }
            } else {
                // Check if we already have a cached IMainFactory for this
                // factory_id. The IMainFactory must be kept alive because the
                // ARAFactory* pointer it returns is owned by it.
                auto cached = ara_main_factories_.find(request.factory_id);
                if (cached != ara_main_factories_.end()) {
                    ara_factory = cached->second->getFactory();
                } else {
                    Steinberg::IPtr<Steinberg::IPluginFactory> factory(
                        module_->getFactory().get());
                    if (factory) {
                        const int32_t count = factory->countClasses();
                        for (int32_t i = 0; i < count && !ara_factory; ++i) {
                            Steinberg::PClassInfo ci{};
                            if (factory->getClassInfo(i, &ci) !=
                                Steinberg::kResultOk)
                                continue;
                            if (strcmp(ci.category, kARAMainFactoryClass) != 0)
                                continue;
                            ARA::IMainFactory* mf = nullptr;
                            factory->createInstance(
                                ci.cid, ARA::IMainFactory::iid.toTUID(),
                                reinterpret_cast<void**>(&mf));
                            if (mf) {
                                const ARA::ARAFactory* f = mf->getFactory();
                                if (f && request.factory_id == f->factoryID) {
                                    // Store with IPtr (addRefs) then transfer
                                    // ownership to the cache. mf already has
                                    // refcount=1 from createInstance.
                                    ara_main_factories_.emplace(
                                        request.factory_id,
                                        Steinberg::IPtr<ARA::IMainFactory>(
                                            mf, false));
                                    ara_factory = f;
                                } else {
                                    mf->release();
                                }
                            }
                        }
                    }
                }
            }

            if (!ara_factory) {
                logger_.log(
                    "WARNING: CreateDocumentController: could not find "
                    "ARAFactory for factory_id=\"" +
                    request.factory_id + "\" (instance_id=" +
                    std::to_string(request.instance_id) + ")");
                return UniversalTResult(Steinberg::kResultFalse);
            }

            // ARA requires initializeARAWithConfiguration() before the first
            // createDocumentControllerWithDocument() call. Call it once per factory ID.
            if (ara_initialized_factories_.find(request.factory_id) ==
                ara_initialized_factories_.end()) {
                // Pick the highest API generation the factory supports, capped
                // at kARAAPIGeneration_2_0_Final (the highest we implement).
                ARA::ARAAPIGeneration desired =
                    ARA::kARAAPIGeneration_2_0_Final;
                if (desired > ara_factory->highestSupportedApiGeneration)
                    desired = ara_factory->highestSupportedApiGeneration;

                ARA::ARAInterfaceConfiguration cfg{};
                cfg.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
                    ARAInterfaceConfiguration, assertFunctionAddress);
                cfg.desiredApiGeneration = desired;
                cfg.assertFunctionAddress = nullptr;
                ara_factory->initializeARAWithConfiguration(&cfg);
                ara_initialized_factories_.insert(request.factory_id);
            }

            auto dc_instance = std::make_unique<AraDocumentControllerInstance>(
                nullptr, request.ara_dc_id, *this);

            dc_instance->host_instance =
                dc_instance->host_proxy.build_host_instance();

            ARA::ARADocumentProperties props{};
            props.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
                ARADocumentProperties, name);
            const std::string& doc_name =
                request.document_properties.name;
            props.name = doc_name.empty() ? nullptr : doc_name.c_str();

            // Insert into the map before calling createDocumentControllerWithDocument
            // so that any host callbacks fired during construction can resolve ara_dc_id.
            AraDocumentControllerInstance* dc_ptr;
            {
                std::lock_guard lock(ara_document_controllers_mutex_);
                auto& slot = ara_document_controllers_[request.ara_dc_id];
                slot = std::move(dc_instance);
                dc_ptr = slot.get();
            }

            const ARA::ARADocumentControllerInstance* result =
                main_context_.run_in_context(
                    [&]() -> const ARA::ARADocumentControllerInstance* {
                        return ara_factory->createDocumentControllerWithDocument(
                            &dc_ptr->host_instance, &props);
                    }).get();

            if (!result) {
                logger_.log(
                    "WARNING: createDocumentControllerWithDocument() returned "
                    "null for factory_id=\"" +
                    request.factory_id + "\" (instance_id=" +
                    std::to_string(request.instance_id) + ")");
                std::lock_guard lock(ara_document_controllers_mutex_);
                ara_document_controllers_.erase(request.ara_dc_id);
                return UniversalTResult(Steinberg::kResultFalse);
            }

            dc_ptr->dc_ref = result->documentControllerRef;
            dc_ptr->dc_instance = result;

            return static_cast<uint64_t>(request.ara_dc_id);
        },
        [&](const YaAra::DestroyDocumentController& request)
            -> YaAra::DestroyDocumentController::Response {
            std::unique_ptr<AraDocumentControllerInstance> entry;
            {
                std::lock_guard lock(ara_document_controllers_mutex_);
                auto it =
                    ara_document_controllers_.find(request.ara_dc_id);
                if (it == ara_document_controllers_.end()) {
                    logger_.log(
                        "WARNING: DestroyDocumentController called with "
                        "unknown ara_dc_id");
                    return Ack{};
                }
                entry = std::move(it->second);
                ara_document_controllers_.erase(it);
            }
            if (entry && entry->dc_instance) {
                if (main_context_.is_gui_thread()) {
                    entry->dc_instance->documentControllerInterface
                        ->destroyDocumentController(
                            entry->dc_instance->documentControllerRef);
                } else {
                    main_context_
                        .run_in_context([&entry]() {
                            entry->dc_instance->documentControllerInterface
                                ->destroyDocumentController(
                                    entry->dc_instance->documentControllerRef);
                        })
                        .get();
                }
            }
            return Ack{};
        },
        [&](const YaAra::BeginEditing& r) -> YaAra::BeginEditing::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (dc && dc->dc_instance)
                dc_call(dc, [](auto* iface, auto dcr) {
                    iface->beginEditing(dcr);
                });
            return Ack{};
        },
        [&](const YaAra::EndEditing& r) -> YaAra::EndEditing::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (dc && dc->dc_instance)
                dc_call(dc, [](auto* iface, auto dcr) {
                    iface->endEditing(dcr);
                });
            return Ack{};
        },
        [&](const YaAra::NotifyModelUpdates& r)
            -> YaAra::NotifyModelUpdates::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (dc && dc->dc_instance)
                dc_call(dc, [](auto* iface, auto dcr) {
                    iface->notifyModelUpdates(dcr);
                });
            return Ack{};
        },
        [&](const YaAra::UpdateDocumentProperties& r)
            -> YaAra::UpdateDocumentProperties::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return Ack{};
            const std::string name = r.properties.name;
            dc_call(dc, [&name](auto* iface, auto dcr) {
                ARA::ARADocumentProperties props{};
                props.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
                    ARADocumentProperties, name);
                props.name = name.empty() ? nullptr : name.c_str();
                iface->updateDocumentProperties(dcr, &props);
            });
            return Ack{};
        },
        [&](const YaAra::AddMusicalContext& r)
            -> YaAra::AddMusicalContext::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return UniversalTResult(Steinberg::kResultFalse);
            const std::string name_s = r.properties.name.value_or(std::string{});
            const bool has_name = r.properties.name.has_value();
            const auto host_ref = r.host_ref;
            const auto order_index = r.properties.order_index;
            const auto color_opt = r.properties.color;
            auto* ref = dc_call(dc, [&](auto* iface, auto dcr)
                                    -> ARA::ARAMusicalContextRef {
                ARA::ARAMusicalContextProperties props{};
                props.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
                    ARAMusicalContextProperties, color);
                props.name = has_name ? name_s.c_str() : nullptr;
                props.orderIndex = order_index;
                ARA::ARAColor color{};
                if (color_opt) {
                    color = {color_opt->r, color_opt->g, color_opt->b};
                    props.color = &color;
                }
                return iface->createMusicalContext(
                    dcr,
                    reinterpret_cast<ARA::ARAMusicalContextHostRef>(host_ref),
                    &props);
            });
            if (!ref)
                return UniversalTResult(Steinberg::kResultFalse);
            return reinterpret_cast<uint64_t>(ref);
        },
        [&](const YaAra::UpdateMusicalContextProperties& r)
            -> YaAra::UpdateMusicalContextProperties::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return Ack{};
            const std::string name_s = r.properties.name.value_or(std::string{});
            const bool has_name = r.properties.name.has_value();
            const auto musical_context_ref = r.musical_context_ref;
            const auto order_index = r.properties.order_index;
            const auto color_opt = r.properties.color;
            dc_call(dc, [&](auto* iface, auto dcr) {
                ARA::ARAMusicalContextProperties props{};
                props.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
                    ARAMusicalContextProperties, color);
                props.name = has_name ? name_s.c_str() : nullptr;
                props.orderIndex = order_index;
                ARA::ARAColor color{};
                if (color_opt) {
                    color = {color_opt->r, color_opt->g, color_opt->b};
                    props.color = &color;
                }
                iface->updateMusicalContextProperties(
                    dcr,
                    reinterpret_cast<ARA::ARAMusicalContextRef>(
                        musical_context_ref),
                    &props);
            });
            return Ack{};
        },
        [&](const YaAra::UpdateMusicalContextContent& r)
            -> YaAra::UpdateMusicalContextContent::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return Ack{};
            const auto musical_context_ref = r.musical_context_ref;
            const auto range_opt = r.range;
            const auto flags = r.flags;
            dc_call(dc, [&](auto* iface, auto dcr) {
                ARA::ARAContentTimeRange range_s{};
                const ARA::ARAContentTimeRange* range_ptr = nullptr;
                if (range_opt) {
                    range_s = {range_opt->start, range_opt->duration};
                    range_ptr = &range_s;
                }
                iface->updateMusicalContextContent(
                    dcr,
                    reinterpret_cast<ARA::ARAMusicalContextRef>(
                        musical_context_ref),
                    range_ptr,
                    static_cast<ARA::ARAContentUpdateFlags>(flags));
            });
            return Ack{};
        },
        [&](const YaAra::RemoveMusicalContext& r)
            -> YaAra::RemoveMusicalContext::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return Ack{};
            const auto musical_context_ref = r.musical_context_ref;
            dc_call(dc, [&](auto* iface, auto dcr) {
                iface->destroyMusicalContext(
                    dcr,
                    reinterpret_cast<ARA::ARAMusicalContextRef>(
                        musical_context_ref));
            });
            return Ack{};
        },
        [&](const YaAra::AddRegionSequence& r)
            -> YaAra::AddRegionSequence::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return UniversalTResult(Steinberg::kResultFalse);
            const auto host_ref = r.host_ref;
            const auto order_index = r.properties.order_index;
            const auto musical_ctx_ref = r.properties.musical_context_ref;
            const auto color_opt = r.properties.color;
            const std::optional<std::string> name_opt = r.properties.name;
            auto* ref = dc_call(dc, [&](auto* iface, auto dcr)
                                    -> ARA::ARARegionSequenceRef {
                ARA::ARARegionSequenceProperties props{};
                props.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
                    ARARegionSequenceProperties, color);
                props.name = name_opt ? name_opt->c_str() : nullptr;
                props.orderIndex = order_index;
                props.musicalContextRef =
                    reinterpret_cast<ARA::ARAMusicalContextRef>(musical_ctx_ref);
                ARA::ARAColor color{};
                if (color_opt) {
                    color = {color_opt->r, color_opt->g, color_opt->b};
                    props.color = &color;
                }
                return iface->createRegionSequence(
                    dcr,
                    reinterpret_cast<ARA::ARARegionSequenceHostRef>(host_ref),
                    &props);
            });
            if (!ref)
                return UniversalTResult(Steinberg::kResultFalse);
            return reinterpret_cast<uint64_t>(ref);
        },
        [&](const YaAra::UpdateRegionSequenceProperties& r)
            -> YaAra::UpdateRegionSequenceProperties::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return Ack{};
            const auto region_sequence_ref = r.region_sequence_ref;
            const auto order_index = r.properties.order_index;
            const auto musical_ctx_ref = r.properties.musical_context_ref;
            const auto color_opt = r.properties.color;
            const std::optional<std::string> name_opt = r.properties.name;
            dc_call(dc, [&](auto* iface, auto dcr) {
                ARA::ARARegionSequenceProperties props{};
                props.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
                    ARARegionSequenceProperties, color);
                props.name = name_opt ? name_opt->c_str() : nullptr;
                props.orderIndex = order_index;
                props.musicalContextRef =
                    reinterpret_cast<ARA::ARAMusicalContextRef>(musical_ctx_ref);
                ARA::ARAColor color{};
                if (color_opt) {
                    color = {color_opt->r, color_opt->g, color_opt->b};
                    props.color = &color;
                }
                iface->updateRegionSequenceProperties(
                    dcr,
                    reinterpret_cast<ARA::ARARegionSequenceRef>(
                        region_sequence_ref),
                    &props);
            });
            return Ack{};
        },
        [&](const YaAra::RemoveRegionSequence& r)
            -> YaAra::RemoveRegionSequence::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return Ack{};
            const auto region_sequence_ref = r.region_sequence_ref;
            dc_call(dc, [&](auto* iface, auto dcr) {
                iface->destroyRegionSequence(
                    dcr,
                    reinterpret_cast<ARA::ARARegionSequenceRef>(
                        region_sequence_ref));
            });
            return Ack{};
        },
        [&](const YaAra::AddAudioSource& r)
            -> YaAra::AddAudioSource::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return UniversalTResult(Steinberg::kResultFalse);
            const uint64_t host_ref = r.host_ref;
            const std::string name_s = r.properties.name.value_or(std::string{});
            const bool has_name = r.properties.name.has_value();
            const auto props_copy = r.properties;
            auto* ref = dc_call(dc, [&](auto* iface, auto dcr)
                                    -> ARA::ARAAudioSourceRef {
                ARA::ARAAudioSourceProperties props{};
                Steinberg::Vst::SpeakerArrangement speaker_arr{};
                if (props_copy.channel_arrangement &&
                    props_copy.channel_arrangement->data_type ==
                        static_cast<int32_t>(
                            ARA::kARAChannelArrangementVST3SpeakerArrangement) &&
                    props_copy.channel_arrangement->data.size() >=
                        sizeof(speaker_arr)) {
                    std::memcpy(&speaker_arr,
                                props_copy.channel_arrangement->data.data(),
                                sizeof(speaker_arr));
                    props.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
                        ARAAudioSourceProperties, channelArrangement);
                    props.channelArrangementDataType =
                        ARA::kARAChannelArrangementVST3SpeakerArrangement;
                    props.channelArrangement = &speaker_arr;
                } else {
                    props.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
                        ARAAudioSourceProperties, merits64BitSamples);
                    props.channelArrangementDataType =
                        ARA::kARAChannelArrangementUndefined;
                    props.channelArrangement = nullptr;
                }
                props.name = has_name ? name_s.c_str() : nullptr;
                props.persistentID = props_copy.persistent_id.c_str();
                props.sampleCount = props_copy.sample_count;
                props.sampleRate = props_copy.sample_rate;
                props.channelCount = props_copy.channel_count;
                props.merits64BitSamples =
                    static_cast<ARA::ARABool>(props_copy.merits_64bit_samples);
                return iface->createAudioSource(
                    dcr,
                    reinterpret_cast<ARA::ARAAudioSourceHostRef>(host_ref),
                    &props);
            });
            if (!ref)
                return UniversalTResult(Steinberg::kResultFalse);
            dc->audio_source_count++;
            return reinterpret_cast<uint64_t>(ref);
        },
        [&](const YaAra::UpdateAudioSourceProperties& r)
            -> YaAra::UpdateAudioSourceProperties::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return Ack{};
            const uint64_t audio_source_ref = r.audio_source_ref;
            const std::string name_s = r.properties.name.value_or(std::string{});
            const bool has_name = r.properties.name.has_value();
            const auto props_copy = r.properties;
            dc_call(dc, [&](auto* iface, auto dcr) {
                ARA::ARAAudioSourceProperties props{};
                Steinberg::Vst::SpeakerArrangement speaker_arr{};
                if (props_copy.channel_arrangement &&
                    props_copy.channel_arrangement->data_type ==
                        static_cast<int32_t>(
                            ARA::kARAChannelArrangementVST3SpeakerArrangement) &&
                    props_copy.channel_arrangement->data.size() >=
                        sizeof(speaker_arr)) {
                    std::memcpy(&speaker_arr,
                                props_copy.channel_arrangement->data.data(),
                                sizeof(speaker_arr));
                    props.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
                        ARAAudioSourceProperties, channelArrangement);
                    props.channelArrangementDataType =
                        ARA::kARAChannelArrangementVST3SpeakerArrangement;
                    props.channelArrangement = &speaker_arr;
                } else {
                    props.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
                        ARAAudioSourceProperties, merits64BitSamples);
                    props.channelArrangementDataType =
                        ARA::kARAChannelArrangementUndefined;
                    props.channelArrangement = nullptr;
                }
                props.name = has_name ? name_s.c_str() : nullptr;
                props.persistentID = props_copy.persistent_id.c_str();
                props.sampleCount = props_copy.sample_count;
                props.sampleRate = props_copy.sample_rate;
                props.channelCount = props_copy.channel_count;
                props.merits64BitSamples =
                    static_cast<ARA::ARABool>(props_copy.merits_64bit_samples);
                iface->updateAudioSourceProperties(
                    dcr,
                    reinterpret_cast<ARA::ARAAudioSourceRef>(audio_source_ref),
                    &props);
            });
            return Ack{};
        },
        [&](const YaAra::UpdateAudioSourceContent& r)
            -> YaAra::UpdateAudioSourceContent::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return Ack{};
            const auto audio_source_ref = r.audio_source_ref;
            const auto range_opt = r.range;
            const auto flags = r.flags;
            dc_call(dc, [&](auto* iface, auto dcr) {
                ARA::ARAContentTimeRange range_s{};
                const ARA::ARAContentTimeRange* range_ptr = nullptr;
                if (range_opt) {
                    range_s = {range_opt->start, range_opt->duration};
                    range_ptr = &range_s;
                }
                iface->updateAudioSourceContent(
                    dcr,
                    reinterpret_cast<ARA::ARAAudioSourceRef>(audio_source_ref),
                    range_ptr,
                    static_cast<ARA::ARAContentUpdateFlags>(flags));
            });
            return Ack{};
        },
        [&](const YaAra::EnableAudioSourceSamplesAccess& r)
            -> YaAra::EnableAudioSourceSamplesAccess::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return Ack{};
            const auto audio_source_ref = r.audio_source_ref;
            const auto enable = r.enable;
            dc_call(dc, [&](auto* iface, auto dcr) {
                iface->enableAudioSourceSamplesAccess(
                    dcr,
                    reinterpret_cast<ARA::ARAAudioSourceRef>(audio_source_ref),
                    static_cast<ARA::ARABool>(enable));
            });
            return Ack{};
        },
        [&](const YaAra::DeactivateAndUnregisterAudioSource& r)
            -> YaAra::DeactivateAndUnregisterAudioSource::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return Ack{};
            const auto audio_source_ref = r.audio_source_ref;
            const auto deactivate = r.deactivate;
            dc_call(dc, [&](auto* iface, auto dcr) {
                iface->deactivateAudioSourceForUndoHistory(
                    dcr,
                    reinterpret_cast<ARA::ARAAudioSourceRef>(audio_source_ref),
                    static_cast<ARA::ARABool>(deactivate));
            });
            return Ack{};
        },
        [&](const YaAra::RemoveAudioSource& r)
            -> YaAra::RemoveAudioSource::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return Ack{};
            const auto audio_source_ref = r.audio_source_ref;
            dc_call(dc, [&](auto* iface, auto dcr) {
                iface->destroyAudioSource(
                    dcr,
                    reinterpret_cast<ARA::ARAAudioSourceRef>(audio_source_ref));
            });
            dc->audio_source_count--;
            return Ack{};
        },
        [&](const YaAra::AddAudioModification& r)
            -> YaAra::AddAudioModification::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return UniversalTResult(Steinberg::kResultFalse);
            const uint64_t audio_source_ref = r.audio_source_ref;
            const uint64_t host_ref = r.host_ref;
            const std::string name_s = r.properties.name.value_or(std::string{});
            const bool has_name = r.properties.name.has_value();
            const std::string pid = r.properties.persistent_id;
            auto* ref = dc_call(dc, [&](auto* iface, auto dcr)
                                    -> ARA::ARAAudioModificationRef {
                ARA::ARAAudioModificationProperties props{};
                props.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
                    ARAAudioModificationProperties, persistentID);
                props.name = has_name ? name_s.c_str() : nullptr;
                props.persistentID = pid.c_str();
                return iface->createAudioModification(
                    dcr,
                    reinterpret_cast<ARA::ARAAudioSourceRef>(audio_source_ref),
                    reinterpret_cast<ARA::ARAAudioModificationHostRef>(host_ref),
                    &props);
            });
            if (!ref)
                return UniversalTResult(Steinberg::kResultFalse);
            return reinterpret_cast<uint64_t>(ref);
        },
        [&](const YaAra::CloneAudioModification& r)
            -> YaAra::CloneAudioModification::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return UniversalTResult(Steinberg::kResultFalse);
            const uint64_t audio_modification_ref = r.audio_modification_ref;
            const uint64_t host_ref = r.host_ref;
            const std::string name_s = r.properties.name.value_or(std::string{});
            const bool has_name = r.properties.name.has_value();
            const std::string pid = r.properties.persistent_id;
            auto* ref = dc_call(dc, [&](auto* iface, auto dcr)
                                    -> ARA::ARAAudioModificationRef {
                ARA::ARAAudioModificationProperties props{};
                props.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
                    ARAAudioModificationProperties, persistentID);
                props.name = has_name ? name_s.c_str() : nullptr;
                props.persistentID = pid.c_str();
                return iface->cloneAudioModification(
                    dcr,
                    reinterpret_cast<ARA::ARAAudioModificationRef>(
                        audio_modification_ref),
                    reinterpret_cast<ARA::ARAAudioModificationHostRef>(host_ref),
                    &props);
            });
            if (!ref)
                return UniversalTResult(Steinberg::kResultFalse);
            return reinterpret_cast<uint64_t>(ref);
        },
        [&](const YaAra::UpdateAudioModificationProperties& r)
            -> YaAra::UpdateAudioModificationProperties::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return Ack{};
            const uint64_t audio_modification_ref = r.audio_modification_ref;
            const std::string name_s = r.properties.name.value_or(std::string{});
            const bool has_name = r.properties.name.has_value();
            const std::string pid = r.properties.persistent_id;
            dc_call(dc, [&](auto* iface, auto dcr) {
                ARA::ARAAudioModificationProperties props{};
                props.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
                    ARAAudioModificationProperties, persistentID);
                props.name = has_name ? name_s.c_str() : nullptr;
                props.persistentID = pid.c_str();
                iface->updateAudioModificationProperties(
                    dcr,
                    reinterpret_cast<ARA::ARAAudioModificationRef>(
                        audio_modification_ref),
                    &props);
            });
            return Ack{};
        },
        [&](const YaAra::DeactivateAndUnregisterAudioModification& r)
            -> YaAra::DeactivateAndUnregisterAudioModification::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return Ack{};
            const auto audio_modification_ref = r.audio_modification_ref;
            const auto deactivate = r.deactivate;
            dc_call(dc, [&](auto* iface, auto dcr) {
                iface->deactivateAudioModificationForUndoHistory(
                    dcr,
                    reinterpret_cast<ARA::ARAAudioModificationRef>(
                        audio_modification_ref),
                    static_cast<ARA::ARABool>(deactivate));
            });
            return Ack{};
        },
        [&](const YaAra::RemoveAudioModification& r)
            -> YaAra::RemoveAudioModification::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return Ack{};
            const auto audio_modification_ref = r.audio_modification_ref;
            dc_call(dc, [&](auto* iface, auto dcr) {
                iface->destroyAudioModification(
                    dcr,
                    reinterpret_cast<ARA::ARAAudioModificationRef>(
                        audio_modification_ref));
            });
            return Ack{};
        },
        [&](const YaAra::AddPlaybackRegion& r)
            -> YaAra::AddPlaybackRegion::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return UniversalTResult(Steinberg::kResultFalse);
            const uint64_t audio_modification_ref = r.audio_modification_ref;
            const uint64_t host_ref = r.host_ref;
            const std::string name_s = r.properties.name.value_or(std::string{});
            const bool has_name = r.properties.name.has_value();
            const auto props_copy = r.properties;
            auto* ref = dc_call(dc, [&](auto* iface, auto dcr)
                                    -> ARA::ARAPlaybackRegionRef {
                ARA::ARAPlaybackRegionProperties props{};
                props.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
                    ARAPlaybackRegionProperties, color);
                props.transformationFlags =
                    static_cast<ARA::ARAPlaybackTransformationFlags>(
                        props_copy.transformation_flags);
                props.startInModificationTime =
                    props_copy.start_in_modification_time;
                props.durationInModificationTime =
                    props_copy.duration_in_modification_time;
                props.startInPlaybackTime = props_copy.start_in_playback_time;
                props.durationInPlaybackTime =
                    props_copy.duration_in_playback_time;
                props.musicalContextRef =
                    reinterpret_cast<ARA::ARAMusicalContextRef>(
                        props_copy.musical_context_ref);
                props.regionSequenceRef =
                    reinterpret_cast<ARA::ARARegionSequenceRef>(
                        props_copy.region_sequence_ref);
                props.name = has_name ? name_s.c_str() : nullptr;
                ARA::ARAColor color{};
                if (props_copy.color) {
                    color = {props_copy.color->r, props_copy.color->g,
                             props_copy.color->b};
                    props.color = &color;
                }
                return iface->createPlaybackRegion(
                    dcr,
                    reinterpret_cast<ARA::ARAAudioModificationRef>(
                        audio_modification_ref),
                    reinterpret_cast<ARA::ARAPlaybackRegionHostRef>(host_ref),
                    &props);
            });
            if (!ref)
                return UniversalTResult(Steinberg::kResultFalse);
            return reinterpret_cast<uint64_t>(ref);
        },
        [&](const YaAra::UpdatePlaybackRegionProperties& r)
            -> YaAra::UpdatePlaybackRegionProperties::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return Ack{};
            const uint64_t playback_region_ref = r.playback_region_ref;
            const std::string name_s = r.properties.name.value_or(std::string{});
            const bool has_name = r.properties.name.has_value();
            const auto props_copy = r.properties;
            dc_call(dc, [&](auto* iface, auto dcr) {
                ARA::ARAPlaybackRegionProperties props{};
                props.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
                    ARAPlaybackRegionProperties, color);
                props.transformationFlags =
                    static_cast<ARA::ARAPlaybackTransformationFlags>(
                        props_copy.transformation_flags);
                props.startInModificationTime =
                    props_copy.start_in_modification_time;
                props.durationInModificationTime =
                    props_copy.duration_in_modification_time;
                props.startInPlaybackTime = props_copy.start_in_playback_time;
                props.durationInPlaybackTime =
                    props_copy.duration_in_playback_time;
                props.musicalContextRef =
                    reinterpret_cast<ARA::ARAMusicalContextRef>(
                        props_copy.musical_context_ref);
                props.regionSequenceRef =
                    reinterpret_cast<ARA::ARARegionSequenceRef>(
                        props_copy.region_sequence_ref);
                props.name = has_name ? name_s.c_str() : nullptr;
                ARA::ARAColor color{};
                if (props_copy.color) {
                    color = {props_copy.color->r, props_copy.color->g,
                             props_copy.color->b};
                    props.color = &color;
                }
                iface->updatePlaybackRegionProperties(
                    dcr,
                    reinterpret_cast<ARA::ARAPlaybackRegionRef>(
                        playback_region_ref),
                    &props);
            });
            return Ack{};
        },
        [&](const YaAra::RemovePlaybackRegion& r)
            -> YaAra::RemovePlaybackRegion::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return Ack{};
            const auto playback_region_ref = r.playback_region_ref;
            dc_call(dc, [&](auto* iface, auto dcr) {
                iface->destroyPlaybackRegion(
                    dcr,
                    reinterpret_cast<ARA::ARAPlaybackRegionRef>(
                        playback_region_ref));
            });
            return Ack{};
        },
        [&](const YaAra::RequestAudioSourceContentAnalysis& r)
            -> YaAra::RequestAudioSourceContentAnalysis::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return Ack{};
            const auto audio_source_ref = r.audio_source_ref;
            std::vector<ARA::ARAContentType> types;
            types.reserve(r.content_types.size());
            for (auto t : r.content_types)
                types.push_back(static_cast<ARA::ARAContentType>(t));
            dc_call(dc, [&](auto* iface, auto dcr) {
                iface->requestAudioSourceContentAnalysis(
                    dcr,
                    reinterpret_cast<ARA::ARAAudioSourceRef>(audio_source_ref),
                    static_cast<ARA::ARASize>(types.size()),
                    types.data());
            });
            return Ack{};
        },
        [&](const YaAra::IsAudioSourceContentAvailableDC& r)
            -> YaAra::IsAudioSourceContentAvailableDC::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return {0};
            return {static_cast<int32_t>(
                dc->dc_instance->documentControllerInterface
                    ->isAudioSourceContentAvailable(
                        dc->dc_instance->documentControllerRef,
                        reinterpret_cast<ARA::ARAAudioSourceRef>(
                            r.audio_source_ref),
                        static_cast<ARA::ARAContentType>(r.content_type)))};
        },
        [&](const YaAra::GetAudioSourceContentGradeDC& r)
            -> YaAra::GetAudioSourceContentGradeDC::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return {0};
            return {static_cast<int32_t>(
                dc->dc_instance->documentControllerInterface
                    ->getAudioSourceContentGrade(
                        dc->dc_instance->documentControllerRef,
                        reinterpret_cast<ARA::ARAAudioSourceRef>(
                            r.audio_source_ref),
                        static_cast<ARA::ARAContentType>(r.content_type)))};
        },
        [&](const YaAra::CreateAudioSourceContentReaderDC& r)
            -> YaAra::CreateAudioSourceContentReaderDC::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return {0};
            ARA::ARAContentTimeRange range_s{};
            const ARA::ARAContentTimeRange* range_ptr = nullptr;
            if (r.range) {
                range_s = {r.range->start, r.range->duration};
                range_ptr = &range_s;
            }
            auto* reader =
                dc->dc_instance->documentControllerInterface
                    ->createAudioSourceContentReader(
                        dc->dc_instance->documentControllerRef,
                        reinterpret_cast<ARA::ARAAudioSourceRef>(
                            r.audio_source_ref),
                        static_cast<ARA::ARAContentType>(r.content_type),
                        range_ptr);
            return {static_cast<int32_t>(reinterpret_cast<uint64_t>(reader))};
        },
        [&](const YaAra::GetContentReaderEventCountDC& r)
            -> YaAra::GetContentReaderEventCountDC::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return {0};
            return {static_cast<int32_t>(
                dc->dc_instance->documentControllerInterface
                    ->getContentReaderEventCount(
                        dc->dc_instance->documentControllerRef,
                        reinterpret_cast<ARA::ARAContentReaderRef>(
                            r.content_reader_ref)))};
        },
        [&](const YaAra::GetContentReaderDataForEventDC& r)
            -> YaAra::GetContentReaderDataForEventDC::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return {{}};
            const auto ct = static_cast<ARA::ARAContentType>(r.content_type);
            const void* data =
                dc->dc_instance->documentControllerInterface
                    ->getContentReaderDataForEvent(
                        dc->dc_instance->documentControllerRef,
                        reinterpret_cast<ARA::ARAContentReaderRef>(
                            r.content_reader_ref),
                        static_cast<ARA::ARAInt32>(r.event_index));
            if (!data)
                return {{}};
            std::vector<uint8_t> bytes;
            auto copy = [&](const void* src, size_t n) {
                const auto* p = static_cast<const uint8_t*>(src);
                bytes.assign(p, p + n);
            };
            switch (ct) {
                case ARA::kARAContentTypeNotes:
                    copy(data, sizeof(ARA::ARAContentNote));
                    break;
                case ARA::kARAContentTypeTempoEntries:
                    copy(data, sizeof(ARA::ARAContentTempoEntry));
                    break;
                case ARA::kARAContentTypeBarSignatures:
                    copy(data, sizeof(ARA::ARAContentBarSignature));
                    break;
                case ARA::kARAContentTypeStaticTuning: {
                    const auto* e =
                        static_cast<const ARA::ARAContentTuning*>(data);
                    const std::string name = e->name ? std::string(e->name) : std::string{};
                    const uint32_t name_len = static_cast<uint32_t>(name.size());
                    auto push = [&](const void* src, size_t n) {
                        const auto* b = static_cast<const uint8_t*>(src);
                        bytes.insert(bytes.end(), b, b + n);
                    };
                    bytes.reserve(sizeof(e->concertPitchFrequency) +
                                  sizeof(e->tunings) +
                                  sizeof(name_len) + name_len);
                    push(&e->concertPitchFrequency, sizeof(e->concertPitchFrequency));
                    push(&e->tunings, sizeof(e->tunings));
                    push(&name_len, sizeof(name_len));
                    bytes.insert(bytes.end(), name.begin(), name.end());
                    break;
                }
                case ARA::kARAContentTypeKeySignatures: {
                    const auto* e =
                        static_cast<const ARA::ARAContentKeySignature*>(data);
                    const std::string name = e->name ? std::string(e->name) : std::string{};
                    const uint32_t name_len = static_cast<uint32_t>(name.size());
                    auto push = [&](const void* src, size_t n) {
                        const auto* b = static_cast<const uint8_t*>(src);
                        bytes.insert(bytes.end(), b, b + n);
                    };
                    bytes.reserve(sizeof(e->root) + sizeof(e->intervals) +
                                  sizeof(name_len) + name_len +
                                  sizeof(e->position));
                    push(&e->root, sizeof(e->root));
                    push(&e->intervals, sizeof(e->intervals));
                    push(&name_len, sizeof(name_len));
                    bytes.insert(bytes.end(), name.begin(), name.end());
                    push(&e->position, sizeof(e->position));
                    break;
                }
                case ARA::kARAContentTypeSheetChords: {
                    const auto* chord =
                        static_cast<const ARA::ARAContentChord*>(data);
                    copy(chord, sizeof(ARA::ARAContentChord));
                    if (chord->name) {
                        const std::string name(chord->name);
                        bytes.insert(bytes.end(), name.begin(), name.end());
                        bytes.push_back(0);
                    } else {
                        bytes.push_back(0);
                    }
                    break;
                }
                default:
                    logger_.log(
                        "WARNING: GetContentReaderDataForEventDC: unhandled "
                        "content type " +
                        std::to_string(static_cast<int32_t>(ct)));
                    break;
            }
            return {bytes};
        },
        [&](const YaAra::DestroyContentReaderDC& r)
            -> YaAra::DestroyContentReaderDC::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return Ack{};
            dc->dc_instance->documentControllerInterface->destroyContentReader(
                dc->dc_instance->documentControllerRef,
                reinterpret_cast<ARA::ARAContentReaderRef>(
                    r.content_reader_ref));
            return Ack{};
        },
        [&](const YaAra::GetPlaybackRegionHeadAndTailTime& r)
            -> YaAra::GetPlaybackRegionHeadAndTailTime::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return YaAra::GetPlaybackRegionHeadAndTailTime::Response{};
            const auto playback_region_ref = r.playback_region_ref;
            return dc_call(
                dc,
                [&](auto* iface, auto dcr)
                    -> YaAra::GetPlaybackRegionHeadAndTailTime::Response {
                    ARA::ARATimeDuration head = 0.0, tail = 0.0;
                    iface->getPlaybackRegionHeadAndTailTime(
                        dcr,
                        reinterpret_cast<ARA::ARAPlaybackRegionRef>(
                            playback_region_ref),
                        &head, &tail);
                    return {.head_time = head, .tail_time = tail};
                });
        },
        [&](const YaAra::StoreObjectsToArchive& r)
            -> YaAra::StoreObjectsToArchive::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return UniversalTResult(Steinberg::kResultFalse);
            const auto archive_writer_host_ref = r.archive_writer_host_ref;
            const auto filter_opt = r.filter;
            return dc_call(
                dc,
                [&](auto* iface, auto dcr)
                    -> YaAra::StoreObjectsToArchive::Response {
                    auto writer_ref =
                        reinterpret_cast<ARA::ARAArchiveWriterHostRef>(
                            archive_writer_host_ref);
                    ARA::ARAStoreObjectsFilter filter_s{};
                    const ARA::ARAStoreObjectsFilter* filter_ptr = nullptr;
                    std::vector<ARA::ARAAudioSourceRef> src_refs;
                    std::vector<ARA::ARAAudioModificationRef> mod_refs;
                    if (filter_opt) {
                        filter_s.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
                            ARAStoreObjectsFilter, audioModificationRefs);
                        filter_s.documentData = static_cast<ARA::ARABool>(
                            filter_opt->document_data);
                        src_refs.reserve(filter_opt->audio_source_refs.size());
                        for (auto h : filter_opt->audio_source_refs)
                            src_refs.push_back(
                                reinterpret_cast<ARA::ARAAudioSourceRef>(h));
                        mod_refs.reserve(
                            filter_opt->audio_modification_refs.size());
                        for (auto h : filter_opt->audio_modification_refs)
                            mod_refs.push_back(
                                reinterpret_cast<ARA::ARAAudioModificationRef>(
                                    h));
                        filter_s.audioSourceRefsCount =
                            static_cast<ARA::ARASize>(src_refs.size());
                        filter_s.audioSourceRefs =
                            src_refs.empty() ? nullptr : src_refs.data();
                        filter_s.audioModificationRefsCount =
                            static_cast<ARA::ARASize>(mod_refs.size());
                        filter_s.audioModificationRefs =
                            mod_refs.empty() ? nullptr : mod_refs.data();
                        filter_ptr = &filter_s;
                    }
                    return static_cast<int32_t>(
                        iface->storeObjectsToArchive(
                            dcr, writer_ref, filter_ptr));
                });
        },
        [&](const YaAra::RestoreObjectsFromArchive& r)
            -> YaAra::RestoreObjectsFromArchive::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return UniversalTResult(Steinberg::kResultFalse);
            const auto archive_reader_host_ref = r.archive_reader_host_ref;
            const auto filter_opt = r.filter;
            return dc_call(
                dc,
                [&](auto* iface, auto dcr)
                    -> YaAra::RestoreObjectsFromArchive::Response {
                    auto reader_ref =
                        reinterpret_cast<ARA::ARAArchiveReaderHostRef>(
                            archive_reader_host_ref);
                    ARA::ARARestoreObjectsFilter filter_s{};
                    const ARA::ARARestoreObjectsFilter* filter_ptr = nullptr;
                    std::vector<ARA::ARAPersistentID> src_arch, src_cur,
                        mod_arch, mod_cur;
                    if (filter_opt) {
                        filter_s.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
                            ARARestoreObjectsFilter,
                            audioModificationCurrentIDs);
                        filter_s.documentData = static_cast<ARA::ARABool>(
                            filter_opt->document_data);
                        for (auto& s : filter_opt->audio_source_archive_ids)
                            src_arch.push_back(s.c_str());
                        for (auto& s : filter_opt->audio_source_current_ids)
                            src_cur.push_back(s.c_str());
                        for (auto& s :
                             filter_opt->audio_modification_archive_ids)
                            mod_arch.push_back(s.c_str());
                        for (auto& s :
                             filter_opt->audio_modification_current_ids)
                            mod_cur.push_back(s.c_str());
                        if (src_arch.size() != src_cur.size() ||
                            mod_arch.size() != mod_cur.size())
                            return UniversalTResult(Steinberg::kResultFalse);
                        filter_s.audioSourceIDsCount =
                            static_cast<ARA::ARASize>(src_arch.size());
                        filter_s.audioSourceArchiveIDs =
                            src_arch.empty() ? nullptr : src_arch.data();
                        filter_s.audioSourceCurrentIDs =
                            src_cur.empty() ? nullptr : src_cur.data();
                        filter_s.audioModificationIDsCount =
                            static_cast<ARA::ARASize>(mod_arch.size());
                        filter_s.audioModificationArchiveIDs =
                            mod_arch.empty() ? nullptr : mod_arch.data();
                        filter_s.audioModificationCurrentIDs =
                            mod_cur.empty() ? nullptr : mod_cur.data();
                        filter_ptr = &filter_s;
                    }
                    return static_cast<int32_t>(
                        iface->restoreObjectsFromArchive(
                            dcr, reader_ref, filter_ptr));
                });
        },
        [&](const YaAra::StoreDocumentToArchive& r)
            -> YaAra::StoreDocumentToArchive::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return UniversalTResult(Steinberg::kResultFalse);
            const auto archive_writer_host_ref = r.archive_writer_host_ref;
            return dc_call(
                dc,
                [&](auto* iface, auto dcr)
                    -> YaAra::StoreDocumentToArchive::Response {
                    return static_cast<int32_t>(
                        iface->storeDocumentToArchive(
                            dcr,
                            reinterpret_cast<ARA::ARAArchiveWriterHostRef>(
                                archive_writer_host_ref)));
                });
        },
        [&](const YaAra::BeginRestoringDocumentFromArchive& r)
            -> YaAra::BeginRestoringDocumentFromArchive::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return UniversalTResult(Steinberg::kResultFalse);
            const auto archive_reader_host_ref = r.archive_reader_host_ref;
            return dc_call(
                dc,
                [&](auto* iface, auto dcr)
                    -> YaAra::BeginRestoringDocumentFromArchive::Response {
                    return static_cast<int32_t>(
                        iface->beginRestoringDocumentFromArchive(
                            dcr,
                            reinterpret_cast<ARA::ARAArchiveReaderHostRef>(
                                archive_reader_host_ref)));
                });
        },
        [&](const YaAra::EndRestoringDocumentFromArchive& r)
            -> YaAra::EndRestoringDocumentFromArchive::Response {
            auto* dc = resolve_dc(r.ara_dc_id);
            if (!dc || !dc->dc_instance)
                return UniversalTResult(Steinberg::kResultFalse);
            // Melodyne crashes when endRestoring is called with no audio
            // sources in the graph. Archived states with no matching
            // persistentID are a no-op per the ARA spec anyway.
            if (dc->audio_source_count == 0)
                return static_cast<int32_t>(ARA::kARATrue);
            const auto archive_reader_host_ref = r.archive_reader_host_ref;
            return dc_call(
                dc,
                [&](auto* iface, auto dcr)
                    -> YaAra::EndRestoringDocumentFromArchive::Response {
                    return static_cast<int32_t>(
                        iface->endRestoringDocumentFromArchive(
                            dcr,
                            reinterpret_cast<ARA::ARAArchiveReaderHostRef>(
                                archive_reader_host_ref)));
                });
        },
        [&](const YaAra::PluginExtension::PlaybackRendererAddRegion& r)
            -> YaAra::PluginExtension::PlaybackRendererAddRegion::Response {
            const auto& [instance, _] = get_instance(r.instance_id);
            if (const auto* ext = instance.ara_extension_instance.load(
                    std::memory_order_acquire)) {
                if (ext->playbackRendererInterface &&
                    ext->playbackRendererInterface->addPlaybackRegion) {
                    const auto ref = r.playback_renderer_ref;
                    const auto region = r.playback_region_ref;
                    main_context_
                        .run_in_context([ext, ref, region]() {
                            ext->playbackRendererInterface->addPlaybackRegion(
                                reinterpret_cast<ARA::ARAPlaybackRendererRef>(ref),
                                reinterpret_cast<ARA::ARAPlaybackRegionRef>(region));
                        })
                        .get();
                }
            }
            return Ack{};
        },
        [&](const YaAra::PluginExtension::PlaybackRendererRemoveRegion& r)
            -> YaAra::PluginExtension::PlaybackRendererRemoveRegion::Response {
            const auto& [instance, _] = get_instance(r.instance_id);
            if (const auto* ext = instance.ara_extension_instance.load(
                    std::memory_order_acquire)) {
                if (ext->playbackRendererInterface &&
                    ext->playbackRendererInterface->removePlaybackRegion) {
                    const auto ref = r.playback_renderer_ref;
                    const auto region = r.playback_region_ref;
                    main_context_
                        .run_in_context([ext, ref, region]() {
                            ext->playbackRendererInterface->removePlaybackRegion(
                                reinterpret_cast<ARA::ARAPlaybackRendererRef>(ref),
                                reinterpret_cast<ARA::ARAPlaybackRegionRef>(region));
                        })
                        .get();
                }
            }
            return Ack{};
        },
        [&](const YaAra::PluginExtension::EditorRendererAddRegion& r)
            -> YaAra::PluginExtension::EditorRendererAddRegion::Response {
            const auto& [instance, _] = get_instance(r.instance_id);
            if (const auto* ext = instance.ara_extension_instance.load(
                    std::memory_order_acquire)) {
                if (ext->editorRendererInterface &&
                    ext->editorRendererInterface->addPlaybackRegion) {
                    const auto ref = r.editor_renderer_ref;
                    const auto region = r.playback_region_ref;
                    main_context_
                        .run_in_context([ext, ref, region]() {
                            ext->editorRendererInterface->addPlaybackRegion(
                                reinterpret_cast<ARA::ARAEditorRendererRef>(ref),
                                reinterpret_cast<ARA::ARAPlaybackRegionRef>(region));
                        })
                        .get();
                }
            }
            return Ack{};
        },
        [&](const YaAra::PluginExtension::EditorRendererRemoveRegion& r)
            -> YaAra::PluginExtension::EditorRendererRemoveRegion::Response {
            const auto& [instance, _] = get_instance(r.instance_id);
            if (const auto* ext = instance.ara_extension_instance.load(
                    std::memory_order_acquire)) {
                if (ext->editorRendererInterface &&
                    ext->editorRendererInterface->removePlaybackRegion) {
                    const auto ref = r.editor_renderer_ref;
                    const auto region = r.playback_region_ref;
                    main_context_
                        .run_in_context([ext, ref, region]() {
                            ext->editorRendererInterface->removePlaybackRegion(
                                reinterpret_cast<ARA::ARAEditorRendererRef>(ref),
                                reinterpret_cast<ARA::ARAPlaybackRegionRef>(region));
                        })
                        .get();
                }
            }
            return Ack{};
        },
        [&](const YaAra::PluginExtension::EditorRendererAddRegionSequence& r)
            -> YaAra::PluginExtension::EditorRendererAddRegionSequence::Response {
            const auto& [instance, _] = get_instance(r.instance_id);
            if (const auto* ext = instance.ara_extension_instance.load(
                    std::memory_order_acquire)) {
                if (ext->editorRendererInterface &&
                    ext->editorRendererInterface->addRegionSequence) {
                    const auto ref = r.editor_renderer_ref;
                    const auto seq = r.region_sequence_ref;
                    main_context_
                        .run_in_context([ext, ref, seq]() {
                            ext->editorRendererInterface->addRegionSequence(
                                reinterpret_cast<ARA::ARAEditorRendererRef>(ref),
                                reinterpret_cast<ARA::ARARegionSequenceRef>(seq));
                        })
                        .get();
                }
            }
            return Ack{};
        },
        [&](const YaAra::PluginExtension::EditorRendererRemoveRegionSequence& r)
            -> YaAra::PluginExtension::EditorRendererRemoveRegionSequence::Response {
            const auto& [instance, _] = get_instance(r.instance_id);
            if (const auto* ext = instance.ara_extension_instance.load(
                    std::memory_order_acquire)) {
                if (ext->editorRendererInterface &&
                    ext->editorRendererInterface->removeRegionSequence) {
                    const auto ref = r.editor_renderer_ref;
                    const auto seq = r.region_sequence_ref;
                    main_context_
                        .run_in_context([ext, ref, seq]() {
                            ext->editorRendererInterface->removeRegionSequence(
                                reinterpret_cast<ARA::ARAEditorRendererRef>(ref),
                                reinterpret_cast<ARA::ARARegionSequenceRef>(seq));
                        })
                        .get();
                }
            }
            return Ack{};
        },
        [&](const YaAra::PluginExtension::EditorViewNotifySelection& r)
            -> YaAra::PluginExtension::EditorViewNotifySelection::Response {
            auto [instance_ref, lock] = get_instance(r.instance_id);
            auto& instance = instance_ref;
            {
                std::lock_guard sel_lock(instance.last_ara_selection_mutex);
                instance.last_ara_selection = r;
            }
            if (const auto* ext = instance.ara_extension_instance.load(
                    std::memory_order_acquire)) {
                if (ext->editorViewInterface &&
                    ext->editorViewInterface->notifySelection) {
                    std::vector<ARA::ARAPlaybackRegionRef> regions;
                    regions.reserve(r.playback_region_refs.size());
                    for (auto h : r.playback_region_refs)
                        regions.push_back(
                            reinterpret_cast<ARA::ARAPlaybackRegionRef>(h));
                    std::vector<ARA::ARARegionSequenceRef> seqs;
                    seqs.reserve(r.region_sequence_refs.size());
                    for (auto h : r.region_sequence_refs)
                        seqs.push_back(
                            reinterpret_cast<ARA::ARARegionSequenceRef>(h));
                    ARA::ARAContentTimeRange time_range_s{};
                    ARA::ARAViewSelection sel{};
                    sel.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
                        ARAViewSelection, timeRange);
                    sel.playbackRegionRefsCount =
                        static_cast<ARA::ARASize>(regions.size());
                    sel.playbackRegionRefs =
                        regions.empty() ? nullptr : regions.data();
                    sel.regionSequenceRefsCount =
                        static_cast<ARA::ARASize>(seqs.size());
                    sel.regionSequenceRefs =
                        seqs.empty() ? nullptr : seqs.data();
                    if (r.time_range) {
                        time_range_s = {r.time_range->start,
                                        r.time_range->duration};
                        sel.timeRange = &time_range_s;
                    } else {
                        sel.timeRange = nullptr;
                    }
                    ext->editorViewInterface->notifySelection(
                        reinterpret_cast<ARA::ARAEditorViewRef>(
                            r.editor_view_ref),
                        &sel);
                }
            }
            return Ack{};
        },
        [&](const YaAra::PluginExtension::EditorViewNotifyHideRegionSequences& r)
            -> YaAra::PluginExtension::EditorViewNotifyHideRegionSequences::Response {
            const auto& [instance, _] = get_instance(r.instance_id);
            if (const auto* ext = instance.ara_extension_instance.load(
                    std::memory_order_acquire)) {
                if (ext->editorViewInterface &&
                    ext->editorViewInterface->notifyHideRegionSequences) {
                    std::vector<ARA::ARARegionSequenceRef> seqs;
                    seqs.reserve(r.region_sequence_refs.size());
                    for (auto h : r.region_sequence_refs)
                        seqs.push_back(
                            reinterpret_cast<ARA::ARARegionSequenceRef>(h));
                    ext->editorViewInterface->notifyHideRegionSequences(
                        reinterpret_cast<ARA::ARAEditorViewRef>(
                            r.editor_view_ref),
                        static_cast<ARA::ARASize>(seqs.size()),
                        seqs.empty() ? nullptr : seqs.data());
                }
            }
            return Ack{};
        },
#endif  // WITH_ARA
        });
}

bool Vst3Bridge::resize_editor(size_t instance_id,
                               const Steinberg::ViewRect& new_size) {
    const auto& [instance, _] = get_instance(instance_id);

    if (instance.editor) {
        instance.editor->resize(new_size.getWidth(), new_size.getHeight());
        return true;
    } else {
        return false;
    }
}

void Vst3Bridge::notify_plugin_on_new_size(size_t instance_id,
                                           Steinberg::ViewRect& new_size) {
    const auto& [instance, _] = get_instance(instance_id);

    if (instance.plug_view_instance) {
        // Skip if the host already called onSize() with this size during
        // resizeView(). This is detected by checking if last_set_size already
        // matches new_size (the OnSize handler updates last_set_size).
        if (instance.last_set_size.getWidth() == new_size.getWidth() &&
            instance.last_set_size.getHeight() == new_size.getHeight()) {
            return;
        }

        instance.plug_view_instance->plug_view->onSize(&new_size);

        // Update last_set_size so getSize() returns consistent values
        instance.last_set_size = new_size;
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
    // channels. The offsets for each audio channel are in bytes because CLAP
    // allows some ports to be 32-bit only while other others are mixed 32-bit
    // and 64-bit if the plugin opts in to it, and the plugin only knows what
    // format it receives during the process call.
    const bool double_precision =
        instance.process_setup->symbolicSampleSize == Steinberg::Vst::kSample64;
    const size_t sample_size =
        (double_precision ? sizeof(double) : sizeof(float));

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
                current_offset += setup->maxSamplesPerBlock * sample_size;
            }
        }

        return bus_offsets;
    };

    // Creating the audio buffer offsets for every channel in every bus will
    // advance `current_offset` to keep pointing to the starting position for
    // the next channel
    const auto input_bus_offsets = create_bus_offsets(Steinberg::Vst::kInput);
    const auto output_bus_offsets = create_bus_offsets(Steinberg::Vst::kOutput);

    // The size of the buffer is in bytes, and it will depend on whether the
    // host is going to pass 32-bit or 64-bit audio to the plugin
    const uint32_t buffer_size = current_offset;

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
                        return main_context_
                            .run_in_context([&]() -> tresult {
                                const auto& [instance, _] =
                                    get_instance(request.instance_id);

                                // HACK: WA Production Imperfect VST3 somehow
                                //       requires `inputs` to be a valid
                                //       pointer, even if there are no inputs.
                                Steinberg::Vst::SpeakerArrangement
                                    empty_arrangement = 0b00000000;

                                return instance.interfaces.audio_processor
                                    ->setBusArrangements(
                                        request.num_ins > 0
                                            ? request.inputs.data()
                                            : &empty_arrangement,
                                        request.num_ins,
                                        request.num_outs > 0
                                            ? request.outputs.data()
                                            : &empty_arrangement,
                                        request.num_outs);
                            })
                            .get();
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

                        return main_context_
                            .run_in_context([&]() -> tresult {
                                return instance.interfaces.audio_processor
                                    ->setupProcessing(request.setup);
                            })
                            .get();
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
                        //       `Vst3AudioProcessorRequest` object, and we only
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
                        return main_context_
                            .run_in_context([&]() -> tresult {
                                const auto& [instance, _] =
                                    get_instance(request.instance_id);

                                return instance.interfaces.component->setIoMode(
                                    request.mode);
                            })
                            .get();
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
                        return main_context_
                            .run_in_context([&]() -> tresult {
                                const auto& [instance, _] =
                                    get_instance(request.instance_id);

                                return instance.interfaces.component
                                    ->activateBus(request.type, request.dir,
                                                  request.index, request.state);
                            })
                            .get();
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
                        return do_mutual_recursion_on_gui_thread(
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
    // created one during `Vst3PluginProxy::Construct`
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
