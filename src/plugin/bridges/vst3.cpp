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

#include "src/common/serialization/vst3.h"
#include "vst3-impls/context-menu-target.h"
#include "vst3-impls/plugin-factory.h"
#include "vst3-impls/plugin-proxy.h"

using namespace std::literals::string_literals;

Vst3PluginBridge::Vst3PluginBridge()
    : PluginBridge(
          PluginType::vst3,
          [](boost::asio::io_context& io_context, const PluginInfo& info) {
              return Vst3Sockets<std::jthread>(
                  io_context,
                  generate_endpoint_base(info.native_library_path.filename()
                                             .replace_extension("")
                                             .string()),
                  true);
          }),
      logger(generic_logger) {
    log_init_message();

    // This will block until all sockets have been connected to by the Wine VST
    // host
    connect_sockets_guarded();

    // Now that communication is set up the Wine host can send callbacks to this
    // bridge class, and we can send control messages to the Wine host. This
    // messaging mechanism is how we relay the VST3 communication protocol. As a
    // first thing, the Wine VST host will ask us for a copy of the
    // configuration.
    host_callback_handler = std::jthread([&]() {
        sockets.vst_host_callback.receive_messages(
            std::pair<Vst3Logger&, bool>(logger, false),
            overload{
                [&](const Vst3ContextMenuProxy::Destruct& request)
                    -> Vst3ContextMenuProxy::Destruct::Response {
                    assert(
                        plugin_proxies.at(request.owner_instance_id)
                            .get()
                            .unregister_context_menu(request.context_menu_id));

                    return Ack{};
                },
                [&](const WantsConfiguration&) -> WantsConfiguration::Response {
                    return config;
                },
                [&](const YaComponentHandler::BeginEdit& request)
                    -> YaComponentHandler::BeginEdit::Response {
                    return plugin_proxies.at(request.owner_instance_id)
                        .get()
                        .component_handler->beginEdit(request.id);
                },
                [&](const YaComponentHandler::PerformEdit& request)
                    -> YaComponentHandler::PerformEdit::Response {
                    return plugin_proxies.at(request.owner_instance_id)
                        .get()
                        .component_handler->performEdit(
                            request.id, request.value_normalized);
                },
                [&](const YaComponentHandler::EndEdit& request)
                    -> YaComponentHandler::EndEdit::Response {
                    return plugin_proxies.at(request.owner_instance_id)
                        .get()
                        .component_handler->endEdit(request.id);
                },
                [&](const YaComponentHandler::RestartComponent& request)
                    -> YaComponentHandler::RestartComponent::Response {
                    return plugin_proxies.at(request.owner_instance_id)
                        .get()
                        .component_handler->restartComponent(request.flags);
                },
                [&](const YaComponentHandler2::SetDirty& request)
                    -> YaComponentHandler2::SetDirty::Response {
                    return plugin_proxies.at(request.owner_instance_id)
                        .get()
                        .component_handler_2->setDirty(request.state);
                },
                [&](const YaComponentHandler2::RequestOpenEditor& request)
                    -> YaComponentHandler2::RequestOpenEditor::Response {
                    return plugin_proxies.at(request.owner_instance_id)
                        .get()
                        .component_handler_2->requestOpenEditor(
                            request.name.c_str());
                },
                [&](const YaComponentHandler2::StartGroupEdit& request)
                    -> YaComponentHandler2::StartGroupEdit::Response {
                    return plugin_proxies.at(request.owner_instance_id)
                        .get()
                        .component_handler_2->startGroupEdit();
                },
                [&](const YaComponentHandler2::FinishGroupEdit& request)
                    -> YaComponentHandler2::FinishGroupEdit::Response {
                    return plugin_proxies.at(request.owner_instance_id)
                        .get()
                        .component_handler_2->finishGroupEdit();
                },
                [&](const YaComponentHandler3::CreateContextMenu& request)
                    -> YaComponentHandler3::CreateContextMenu::Response {
                    // XXX: As mentioned elsewhere, since VST3 only supports a
                    //      single plug view type at the moment we'll just
                    //      assume that this function is called from the last
                    //      (and only) `IPlugView*` instance returned by the
                    //      plugin.
                    Vst3PlugViewProxyImpl* plug_view =
                        plugin_proxies.at(request.owner_instance_id)
                            .get()
                            .last_created_plug_view;

                    Steinberg::IPtr<Steinberg::Vst::IContextMenu> context_menu =
                        Steinberg::owned(
                            plugin_proxies.at(request.owner_instance_id)
                                .get()
                                .component_handler_3->createContextMenu(
                                    plug_view, request.param_id
                                                   ? &*request.param_id
                                                   : nullptr));

                    if (context_menu) {
                        const size_t context_menu_id =
                            plugin_proxies.at(request.owner_instance_id)
                                .get()
                                .register_context_menu(std::move(context_menu));

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
                                         return plugin_proxies
                                             .at(request.owner_instance_id)
                                             .get()
                                             .component_handler_bus_activation
                                             ->requestBusActivation(
                                                 request.type, request.dir,
                                                 request.index, request.state);
                                     },
                [&](const YaContextMenu::GetItemCount& request)
                    -> YaContextMenu::GetItemCount::Response {
                    return plugin_proxies.at(request.owner_instance_id)
                        .get()
                        .context_menus.at(request.context_menu_id)
                        .menu->getItemCount();
                },
                [&](YaContextMenu::AddItem& request)
                    -> YaContextMenu::AddItem::Response {
                    Vst3PluginProxyImpl::ContextMenu& context_menu =
                        plugin_proxies.at(request.owner_instance_id)
                            .get()
                            .context_menus.at(request.context_menu_id);

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
                    Vst3PluginProxyImpl::ContextMenu& context_menu =
                        plugin_proxies.at(request.owner_instance_id)
                            .get()
                            .context_menus.at(request.context_menu_id);

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
                    // FIXME: In REAPER having the menu open without interacting
                    //        with it causes malloc failures or failing font
                    //        drawing calls. Valgrind reports all kinds of
                    //        memory errors within REAPER when this happens, and
                    //        I'm not sure if yabridge is to blame here. - As it
                    //        turns out a lot of stuff in REAPEr, including
                    //        calls to `IPlugFrame::resizeView()`, are not
                    //        thread safe. We need to hook into `IRunLoop` and
                    //        execute `IContextMenu::popup()` and
                    //        `IPlugFrame::resizeView()` functions from there.
                    return plugin_proxies.at(request.owner_instance_id)
                        .get()
                        .context_menus.at(request.context_menu_id)
                        .menu->popup(request.x, request.y);
                },
                [&](YaConnectionPoint::Notify& request)
                    -> YaConnectionPoint::Notify::Response {
                    return plugin_proxies.at(request.instance_id)
                        .get()
                        .connection_point_proxy->notify(&request.message_ptr);
                },
                [&](const YaHostApplication::GetName& request)
                    -> YaHostApplication::GetName::Response {
                    tresult result;
                    Steinberg::Vst::String128 name{0};
                    if (request.owner_instance_id) {
                        result = plugin_proxies.at(*request.owner_instance_id)
                                     .get()
                                     .host_application->getName(name);
                    } else {
                        result =
                            plugin_factory->host_application->getName(name);
                    }

                    // TODO: Remove this warning ocne Ardour supports multiple
                    //       inputs and outputs
                    if (result == Steinberg::kResultOk && name == u"Ardour"s) {
                        logger.log(
                            "WARNING: Ardour currently does not support "
                            "plugins with multiple inputs or outputs. If you "
                            "get a Wine crash dialog or a plugin causes Ardour "
                            "to freeze, then this is likely the cause.");
                    }

                    return YaHostApplication::GetNameResponse{
                        .result = result,
                        .name = tchar_pointer_to_u16string(name),
                    };
                },
                [&](YaPlugFrame::ResizeView& request)
                    -> YaPlugFrame::ResizeView::Response {
                    // XXX: As mentioned elsewhere, since VST3 only supports a
                    //      single plug view type at the moment we'll just
                    //      assume that this function is called from the last
                    //      (and only) `IPlugView*` instance returned by the
                    //      plugin.
                    Vst3PlugViewProxyImpl* plug_view =
                        plugin_proxies.at(request.owner_instance_id)
                            .get()
                            .last_created_plug_view;

                    return plug_view->plug_frame->resizeView(plug_view,
                                                             &request.new_size);
                },
                [&](const YaPlugInterfaceSupport::IsPlugInterfaceSupported&
                        request) -> YaPlugInterfaceSupport::
                                     IsPlugInterfaceSupported::Response {
                                         // TODO: For correctness' sake we
                                         //       should automatically reject
                                         //       queries for interfaces we
                                         //       don't yet or can't implement,
                                         //       like the ARA interfaces.
                                         if (request.owner_instance_id) {
                                             return plugin_proxies
                                                 .at(*request.owner_instance_id)
                                                 .get()
                                                 .plug_interface_support
                                                 ->isPlugInterfaceSupported(
                                                     request.iid.data());
                                         } else {
                                             return plugin_factory
                                                 ->plug_interface_support
                                                 ->isPlugInterfaceSupported(
                                                     request.iid.data());
                                         }
                                     },
                [&](const YaProgress::Start& request)
                    -> YaProgress::Start::Response {
                    Steinberg::Vst::IProgress::ID out_id;
                    const tresult result =
                        plugin_proxies.at(request.owner_instance_id)
                            .get()
                            .progress->start(
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
                    return plugin_proxies.at(request.owner_instance_id)
                        .get()
                        .progress->update(request.id, request.norm_value);
                },
                [&](const YaProgress::Finish& request)
                    -> YaProgress::Finish::Response {
                    return plugin_proxies.at(request.owner_instance_id)
                        .get()
                        .progress->finish(request.id);
                },
                [&](const YaUnitHandler::NotifyUnitSelection& request)
                    -> YaUnitHandler::NotifyUnitSelection::Response {
                    return plugin_proxies.at(request.owner_instance_id)
                        .get()
                        .unit_handler->notifyUnitSelection(request.unit_id);
                },
                [&](const YaUnitHandler::NotifyProgramListChange& request)
                    -> YaUnitHandler::NotifyProgramListChange::Response {
                    return plugin_proxies.at(request.owner_instance_id)
                        .get()
                        .unit_handler->notifyProgramListChange(
                            request.list_id, request.program_index);
                },
                [&](const YaUnitHandler2::NotifyUnitByBusChange& request)
                    -> YaUnitHandler2::NotifyUnitByBusChange::Response {
                    return plugin_proxies.at(request.owner_instance_id)
                        .get()
                        .unit_handler_2->notifyUnitByBusChange();
                },
            });
    });
}

Vst3PluginBridge::~Vst3PluginBridge() {
    // Drop all work make sure all sockets are closed
    plugin_host->terminate();
    io_context.stop();
}

Steinberg::IPluginFactory* Vst3PluginBridge::get_plugin_factory() {
    // Even though we're working with raw pointers here, we should pretend that
    // we're `IPtr<Steinberg::IPluginFactory>` and do the reference counting
    // ourselves. This should work the same was as the standard implementation
    // in `public.sdk/source/main/pluginfactory.h`. If we were to use an IPtr or
    // an STL smart pointer we would get a double free (or rather, a use after
    // free).
    if (plugin_factory) {
        plugin_factory->addRef();
    } else {
        // Set up the plugin factory, since this is the first thing the host
        // will request after loading the module. Host callback handlers should
        // have started before this since the Wine plugin host will request a
        // copy of the configuration during its initialization.
        YaPluginFactory::ConstructArgs factory_args =
            sockets.host_vst_control.send_message(
                YaPluginFactory::Construct{},
                std::pair<Vst3Logger&, bool>(logger, true));
        plugin_factory =
            new YaPluginFactoryImpl(*this, std::move(factory_args));
    }

    return plugin_factory;
}

void Vst3PluginBridge::register_plugin_proxy(
    Vst3PluginProxyImpl& proxy_object) {
    std::lock_guard lock(plugin_proxies_mutex);

    plugin_proxies.emplace(proxy_object.instance_id(),
                           std::ref<Vst3PluginProxyImpl>(proxy_object));

    // For optimization reaons we use dedicated sockets for functions that will
    // be run in the audio processing loop
    if (proxy_object.YaAudioProcessor::supported() ||
        proxy_object.YaComponent::supported()) {
        sockets.add_audio_processor_and_connect(proxy_object.instance_id());
    }
}

void Vst3PluginBridge::unregister_plugin_proxy(
    Vst3PluginProxyImpl& proxy_object) {
    std::lock_guard lock(plugin_proxies_mutex);

    plugin_proxies.erase(proxy_object.instance_id());
    if (proxy_object.YaAudioProcessor::supported() ||
        proxy_object.YaComponent::supported()) {
        sockets.remove_audio_processor(proxy_object.instance_id());
    }
}
