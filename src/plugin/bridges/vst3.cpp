// yabridge: a Wine VST bridge
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

#include <pluginterfaces/base/ustring.h>

#include "../../common/serialization/vst3.h"
#include "vst3-impls/context-menu-target.h"
#include "vst3-impls/plugin-proxy.h"

using namespace std::literals::string_literals;

Vst3PluginBridge::Vst3PluginBridge()
    : PluginBridge(
          PluginType::vst3,
          [](boost::asio::io_context& io_context, const PluginInfo& info) {
              return Vst3Sockets<std::jthread>(
                  io_context,
                  generate_endpoint_base(info.native_library_path_.filename()
                                             .replace_extension("")
                                             .string()),
                  true);
          }),
      logger_(generic_logger_) {
    log_init_message();

    // This will block until all sockets have been connected to by the Wine VST
    // host
    connect_sockets_guarded();

    // Now that communication is set up the Wine host can send callbacks to this
    // bridge class, and we can send control messages to the Wine host. This
    // messaging mechanism is how we relay the VST3 communication protocol. As a
    // first thing, the Wine VST host will ask us for a copy of the
    // configuration.
    host_callback_handler_ = std::jthread([&]() {
        set_realtime_priority(true);
        pthread_setname_np(pthread_self(), "host-callbacks");

        sockets_.vst_host_callback_.receive_messages(
            std::pair<Vst3Logger&, bool>(logger_, false),
            overload{
                [&](const Vst3ContextMenuProxy::Destruct& request)
                    -> Vst3ContextMenuProxy::Destruct::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    assert(proxy_object.unregister_context_menu(
                        request.context_menu_id));

                    return Ack{};
                },
                [&](const WantsConfiguration& request)
                    -> WantsConfiguration::Response {
                    warn_on_version_mismatch(request.host_version);

                    return config_;
                },
                [&](const YaComponentHandler::BeginEdit& request)
                    -> YaComponentHandler::BeginEdit::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    return proxy_object.component_handler_->beginEdit(
                        request.id);
                },
                [&](const YaComponentHandler::PerformEdit& request)
                    -> YaComponentHandler::PerformEdit::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    return proxy_object.component_handler_->performEdit(
                        request.id, request.value_normalized);
                },
                [&](const YaComponentHandler::EndEdit& request)
                    -> YaComponentHandler::EndEdit::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    return proxy_object.component_handler_->endEdit(request.id);
                },
                [&](const YaComponentHandler::RestartComponent& request)
                    -> YaComponentHandler::RestartComponent::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    // To err on the safe side, we'll just always clear out all
                    // of our caches whenever a plugin requests a restart
                    proxy_object.clear_caches();

                    return proxy_object.component_handler_->restartComponent(
                        request.flags);
                },
                [&](const YaComponentHandler2::SetDirty& request)
                    -> YaComponentHandler2::SetDirty::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    return proxy_object.component_handler_2_->setDirty(
                        request.state);
                },
                [&](const YaComponentHandler2::RequestOpenEditor& request)
                    -> YaComponentHandler2::RequestOpenEditor::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    return proxy_object.component_handler_2_->requestOpenEditor(
                        request.name.c_str());
                },
                [&](const YaComponentHandler2::StartGroupEdit& request)
                    -> YaComponentHandler2::StartGroupEdit::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    return proxy_object.component_handler_2_->startGroupEdit();
                },
                [&](const YaComponentHandler2::FinishGroupEdit& request)
                    -> YaComponentHandler2::FinishGroupEdit::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    return proxy_object.component_handler_2_->finishGroupEdit();
                },
                [&](const YaComponentHandler3::CreateContextMenu& request)
                    -> YaComponentHandler3::CreateContextMenu::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    // XXX: As mentioned elsewhere, since VST3 only supports a
                    //      single plug view type at the moment we'll just
                    //      assume that this function is called from the last
                    //      (and only) `IPlugView*` instance returned by the
                    //      plugin.
                    Vst3PlugViewProxyImpl* plug_view =
                        proxy_object.last_created_plug_view_;

                    Steinberg::IPtr<Steinberg::Vst::IContextMenu> context_menu =
                        Steinberg::owned(
                            proxy_object.component_handler_3_
                                ->createContextMenu(plug_view,
                                                    request.param_id
                                                        ? &*request.param_id
                                                        : nullptr));

                    if (context_menu) {
                        const size_t context_menu_id =
                            proxy_object.register_context_menu(context_menu);

                        return YaComponentHandler3::CreateContextMenuResponse{
                            .context_menu_args =
                                Vst3ContextMenuProxy::ConstructArgs(
                                    context_menu, request.owner_instance_id,
                                    context_menu_id)};
                    } else {
                        return YaComponentHandler3::CreateContextMenuResponse{
                            .context_menu_args = std::nullopt};
                    }
                },
                [&](const YaComponentHandlerBusActivation::RequestBusActivation&
                        request) -> YaComponentHandlerBusActivation::
                                     RequestBusActivation::Response {
                                         const auto& [proxy_object, _] =
                                             get_proxy(
                                                 request.owner_instance_id);

                                         return proxy_object
                                             .component_handler_bus_activation_
                                             ->requestBusActivation(
                                                 request.type, request.dir,
                                                 request.index, request.state);
                                     },
                [&](const YaContextMenu::GetItemCount& request)
                    -> YaContextMenu::GetItemCount::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    return proxy_object.context_menus_
                        .at(request.context_menu_id)
                        .menu->getItemCount();
                },
                [&](YaContextMenu::AddItem& request)
                    -> YaContextMenu::AddItem::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    Vst3PluginProxyImpl::ContextMenu& context_menu =
                        proxy_object.context_menus_.at(request.context_menu_id);

                    if (request.target) {
                        context_menu.targets[request.item.tag] =
                            Steinberg::owned(new YaContextMenuTargetImpl(
                                *this, std::move(*request.target)));

                        return context_menu.menu->addItem(
                            request.item,
                            context_menu.targets[request.item.tag]);
                    } else {
                        return context_menu.menu->addItem(request.item,
                                                          nullptr);
                    }
                },
                [&](const YaContextMenu::RemoveItem& request)
                    -> YaContextMenu::RemoveItem::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    Vst3PluginProxyImpl::ContextMenu& context_menu =
                        proxy_object.context_menus_.at(request.context_menu_id);

                    if (const auto it =
                            context_menu.targets.find(request.item.tag);
                        it != context_menu.targets.end()) {
                        return context_menu.menu->removeItem(request.item,
                                                             it->second);
                    } else {
                        return context_menu.menu->removeItem(request.item,
                                                             nullptr);
                    }
                },
                [&](const YaContextMenu::Popup& request)
                    -> YaContextMenu::Popup::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    // REAPER requires this to be run from its provided event
                    // loop or else it will likely segfault at some point
                    return proxy_object.last_created_plug_view_->run_gui_task(
                        [&, &proxy_object = proxy_object]() -> tresult {
                            return proxy_object.context_menus_
                                .at(request.context_menu_id)
                                .menu->popup(request.x, request.y);
                        });
                },
                [&](YaConnectionPoint::Notify& request)
                    -> YaConnectionPoint::Notify::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.instance_id);

                    return proxy_object.connection_point_proxy_->notify(
                        &request.message_ptr);
                },
                [&](const YaHostApplication::GetName& request)
                    -> YaHostApplication::GetName::Response {
                    tresult result;
                    Steinberg::Vst::String128 name{0};

                    // HACK: Certain plugins may have undesirable DAW-specific
                    //       behaviour. Chromaphone 3 for instance has broken
                    //       text input dialogs when using Bitwig. We can work
                    //       around these issues by reporting we're running
                    //       under some other host. We do this here to stay
                    //       consistent with the VST2 version, where it has to
                    //       be done on the plugin's side.
                    if (config_.hide_daw) {
                        // This is the only sane-ish way to copy a c-style
                        // string to an UTF-16 string buffer
                        Steinberg::UString128(product_name_override)
                            .copyTo(name, 128);

                        result = Steinberg::kResultOk;
                    } else {
                        // There can be a global host context in addition to
                        // plugin-specific host contexts, so we need to call the
                        // function on correct context
                        if (request.owner_instance_id) {
                            const auto& [proxy_object, _] =
                                get_proxy(*request.owner_instance_id);

                            result =
                                proxy_object.host_application_->getName(name);
                        } else {
                            result =
                                plugin_factory_->host_application_->getName(
                                    name);
                        }
                    }

                    return YaHostApplication::GetNameResponse{
                        .result = result,
                        .name = tchar_pointer_to_u16string(name),
                    };
                },
                [&](YaPlugFrame::ResizeView& request)
                    -> YaPlugFrame::ResizeView::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    // XXX: As mentioned elsewhere, since VST3 only supports a
                    //      single plug view type at the moment we'll just
                    //      assume that this function is called from the last
                    //      (and only) `IPlugView*` instance returned by the
                    //      plugin.
                    Vst3PlugViewProxyImpl* plug_view =
                        proxy_object.last_created_plug_view_;

                    // REAPER requires this to be run from its provided event
                    // loop or else it will likely segfault at some point
                    return plug_view->run_gui_task([&]() -> tresult {
                        return plug_view->plug_frame_->resizeView(
                            plug_view, &request.new_size);
                    });
                },
                [&](const YaPlugInterfaceSupport::IsPlugInterfaceSupported&
                        request)
                    -> YaPlugInterfaceSupport::IsPlugInterfaceSupported::
                        Response {
                            // TODO: For correctness' sake we should
                            //       automatically reject queries for interfaces
                            //       we don't yet or can't implement, like the
                            //       ARA interfaces.
                            if (request.owner_instance_id) {
                                const auto& [proxy_object, _] =
                                    get_proxy(*request.owner_instance_id);

                                return proxy_object.plug_interface_support_
                                    ->isPlugInterfaceSupported(
                                        request.iid.get_native_uid().data());
                            } else {
                                return plugin_factory_->plug_interface_support_
                                    ->isPlugInterfaceSupported(
                                        request.iid.get_native_uid().data());
                            }
                        },
                [&](const YaProgress::Start& request)
                    -> YaProgress::Start::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    Steinberg::Vst::IProgress::ID out_id;
                    const tresult result = proxy_object.progress_->start(
                        request.type,
                        request.optional_description
                            ? u16string_to_tchar_pointer(
                                  *request.optional_description)
                            : nullptr,
                        out_id);

                    return YaProgress::StartResponse{.result = result,
                                                     .out_id = out_id};
                },
                [&](const YaProgress::Update& request)
                    -> YaProgress::Update::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    return proxy_object.progress_->update(request.id,
                                                          request.norm_value);
                },
                [&](const YaProgress::Finish& request)
                    -> YaProgress::Finish::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    return proxy_object.progress_->finish(request.id);
                },
                [&](const YaUnitHandler::NotifyUnitSelection& request)
                    -> YaUnitHandler::NotifyUnitSelection::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    return proxy_object.unit_handler_->notifyUnitSelection(
                        request.unit_id);
                },
                [&](const YaUnitHandler::NotifyProgramListChange& request)
                    -> YaUnitHandler::NotifyProgramListChange::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    return proxy_object.unit_handler_->notifyProgramListChange(
                        request.list_id, request.program_index);
                },
                [&](const YaUnitHandler2::NotifyUnitByBusChange& request)
                    -> YaUnitHandler2::NotifyUnitByBusChange::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    return proxy_object.unit_handler_2_
                        ->notifyUnitByBusChange();
                },
            });
    });
}

Vst3PluginBridge::~Vst3PluginBridge() noexcept {
    try {
        // Drop all work make sure all sockets are closed
        plugin_host_->terminate();
        io_context_.stop();
    } catch (const boost::system::system_error&) {
        // It could be that the sockets have already been closed or that the
        // process has already exited (at which point we probably won't be
        // executing this, but maybe if all the stars align)
    }
}

Steinberg::IPluginFactory* Vst3PluginBridge::get_plugin_factory() {
    // This works the same way as the default implementation in
    // `public.sdk/source/main/pluginfactory.h`, with the exception that we back
    // the plugin factory with an `IPtr` ourselves so it cannot be freed before
    // `Vst3PluginBridge` gets freed. This is needed for REAPER as REAPER does
    // not call `ModuleExit()`.
    if (!plugin_factory_) {
        // Set up the plugin factory, since this is the first thing the host
        // will request after loading the module. Host callback handlers should
        // have started before this since the Wine plugin host will request a
        // copy of the configuration during its initialization.
        Vst3PluginFactoryProxy::ConstructArgs factory_args =
            sockets_.host_vst_control_.send_message(
                Vst3PluginFactoryProxy::Construct{},
                std::pair<Vst3Logger&, bool>(logger_, true));
        plugin_factory_ = Steinberg::owned(
            new Vst3PluginFactoryProxyImpl(*this, std::move(factory_args)));
    }

    // Because we're returning a raw pointer, we have to increase the reference
    // count ourselves
    plugin_factory_->addRef();

    return plugin_factory_;
}

std::pair<Vst3PluginProxyImpl&, std::shared_lock<std::shared_mutex>>
Vst3PluginBridge::get_proxy(size_t instance_id) noexcept {
    std::shared_lock lock(plugin_proxies_mutex_);

    return std::pair<Vst3PluginProxyImpl&, std::shared_lock<std::shared_mutex>>(
        plugin_proxies_.at(instance_id).get(), std::move(lock));
}

void Vst3PluginBridge::register_plugin_proxy(
    Vst3PluginProxyImpl& proxy_object) {
    std::unique_lock lock(plugin_proxies_mutex_);

    plugin_proxies_.emplace(proxy_object.instance_id(),
                            std::ref<Vst3PluginProxyImpl>(proxy_object));

    // For optimization reaons we use dedicated sockets for functions that will
    // be run in the audio processing loop
    if (proxy_object.YaAudioProcessor::supported() ||
        proxy_object.YaComponent::supported()) {
        sockets_.add_audio_processor_and_connect(proxy_object.instance_id());
    }
}

void Vst3PluginBridge::unregister_plugin_proxy(
    Vst3PluginProxyImpl& proxy_object) {
    std::lock_guard lock(plugin_proxies_mutex_);

    plugin_proxies_.erase(proxy_object.instance_id());
    if (proxy_object.YaAudioProcessor::supported() ||
        proxy_object.YaComponent::supported()) {
        sockets_.remove_audio_processor(proxy_object.instance_id());
    }
}
