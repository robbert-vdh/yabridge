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

#include <cstring>
#include <limits>

#include <pluginterfaces/base/ustring.h>

#include "../../common/serialization/vst3-impls/context-menu-target.h"
#include "../../common/serialization/vst3.h"
#include "vst3-impls/plugin-proxy.h"
#ifdef WITH_ARA
#include "vst3-impls/ara-document-controller-proxy.h"
#endif

using namespace std::literals::string_literals;

Vst3PluginBridge::Vst3PluginBridge(const ghc::filesystem::path& plugin_path)
    : PluginBridge(
          PluginType::vst3,
          plugin_path,
          [](asio::io_context& io_context, const PluginInfo& info) {
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
    // first thing, the Wine plugin host will ask us for a copy of the
    // configuration.
    host_callback_handler_ = std::jthread([&]() {
        set_realtime_priority(true);
        pthread_setname_np(pthread_self(), "host-callbacks");

#ifdef WITH_ARA
        const auto resolve_proxy =
            [&](native_size_t id) -> std::shared_ptr<AraDocumentControllerProxy> {
            std::lock_guard lock(ara_document_controllers_mutex_);
            auto it = ara_document_controllers_.find(id);
            return it != ara_document_controllers_.end()
                       ? it->second
                       : nullptr;
        };
#endif
        sockets_.plugin_host_callback_.receive_messages(
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
                [&](YaContextMenu::AddItem& request)
                    -> YaContextMenu::AddItem::Response {
                    const auto& [proxy_object, _] =
                        get_proxy(request.owner_instance_id);

                    Vst3PluginProxyImpl::ContextMenu& context_menu =
                        proxy_object.context_menus_.at(request.context_menu_id);

                    if (request.target) {
                        context_menu.plugin_targets[request.item.tag] =
                            Steinberg::owned(new YaContextMenuTargetImpl(
                                *this, std::move(*request.target)));

                        return context_menu.menu->addItem(
                            request.item,
                            context_menu.plugin_targets[request.item.tag]);
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
                            context_menu.plugin_targets.find(request.item.tag);
                        it != context_menu.plugin_targets.end()) {
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
                [&](YaContextMenuTarget::ExecuteMenuItem& request)
                    -> YaContextMenuTarget::ExecuteMenuItem::Response {
                    const auto& [proxy, _] =
                        get_proxy(request.owner_instance_id);

                    // This is of course only used for calling host defined
                    // targets from the plugin, this will never be called when
                    // the plugin calls their own targets for whatever reason
                    Steinberg::Vst::IContextMenuItem item;
                    Steinberg::Vst::IContextMenuTarget* target = nullptr;
                    Steinberg::IPtr<Steinberg::Vst::IContextMenu> menu =
                        proxy.context_menus_.at(request.context_menu_id).menu;
                    if (menu->getItem(request.item_id, item, &target) ==
                            Steinberg::kResultOk &&
                        target) {
                        return target->executeMenuItem(request.tag);
                    } else {
                        logger_.log(
                            "WARNING: A IContextMenuTarget::ExecuteMenuItem "
                            "from the plugin could not be handled");

                        return Steinberg::kInvalidArgument;
                    }
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
#ifdef WITH_ARA
                [&](const YaAra::HostCallback::GetArchiveSize& request)
                    -> YaAra::HostCallback::GetArchiveSize::Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return {0};
                    if (!proxy->host_instance_ ||
                        !proxy->host_instance_->archivingControllerInterface)
                        return {0};
                    auto reader = reinterpret_cast<ARA::ARAArchiveReaderHostRef>(
                        request.archive_reader_host_ref);
                    const ARA::ARASize size =
                        proxy->host_instance_->archivingControllerInterface
                            ->getArchiveSize(
                                proxy->host_instance_->archivingControllerHostRef,
                                reader);
                    return {static_cast<uint64_t>(size)};
                },
                [&](const YaAra::HostCallback::ReadBytesFromArchive& request)
                    -> YaAra::HostCallback::ReadBytesFromArchive::Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return {{}};
                    constexpr uint64_t kMaxAraArchiveReadBytes = 64u * 1024u * 1024u;
                    if (request.length > kMaxAraArchiveReadBytes)
                        return {{}};
                    if (!proxy->host_instance_ ||
                        !proxy->host_instance_->archivingControllerInterface)
                        return {{}};
                    auto reader = reinterpret_cast<ARA::ARAArchiveReaderHostRef>(
                        request.archive_reader_host_ref);
                    std::vector<uint8_t> buf(request.length);
                    const ARA::ARABool ok =
                        proxy->host_instance_->archivingControllerInterface
                            ->readBytesFromArchive(
                                proxy->host_instance_->archivingControllerHostRef,
                                reader,
                                static_cast<ARA::ARASize>(request.position),
                                static_cast<ARA::ARASize>(request.length),
                                buf.data());
                    if (!ok)
                        buf.clear();
                    return {std::move(buf)};
                },
                [&](const YaAra::HostCallback::WriteBytesToArchive& request)
                    -> YaAra::HostCallback::WriteBytesToArchive::Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return {0};
                    if (!proxy->host_instance_ ||
                        !proxy->host_instance_->archivingControllerInterface)
                        return {0};
                    auto writer = reinterpret_cast<ARA::ARAArchiveWriterHostRef>(
                        request.archive_writer_host_ref);
                    const ARA::ARABool ok =
                        proxy->host_instance_->archivingControllerInterface
                            ->writeBytesToArchive(
                                proxy->host_instance_->archivingControllerHostRef,
                                writer,
                                static_cast<ARA::ARASize>(request.position),
                                static_cast<ARA::ARASize>(request.data.size()),
                                request.data.data());
                    return {ok ? 1 : 0};
                },
                [&](const YaAra::HostCallback::NotifyDocumentArchivingProgress&
                        request)
                    -> YaAra::HostCallback::NotifyDocumentArchivingProgress::
                        Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return Ack{};
                    if (proxy->host_instance_ &&
                        proxy->host_instance_->archivingControllerInterface)
                        proxy->host_instance_->archivingControllerInterface
                            ->notifyDocumentArchivingProgress(
                                proxy->host_instance_->archivingControllerHostRef,
                                request.value);
                    return Ack{};
                },
                [&](const YaAra::HostCallback::NotifyDocumentUnarchivingProgress&
                        request)
                    -> YaAra::HostCallback::NotifyDocumentUnarchivingProgress::
                        Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return Ack{};
                    if (proxy->host_instance_ &&
                        proxy->host_instance_->archivingControllerInterface)
                        proxy->host_instance_->archivingControllerInterface
                            ->notifyDocumentUnarchivingProgress(
                                proxy->host_instance_->archivingControllerHostRef,
                                request.value);
                    return Ack{};
                },
                [&](const YaAra::HostCallback::GetDocumentArchiveID& request)
                    -> YaAra::HostCallback::GetDocumentArchiveID::Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return {{}};
                    if (!proxy->host_instance_ ||
                        !proxy->host_instance_->archivingControllerInterface)
                        return {{}};
                    auto reader = reinterpret_cast<ARA::ARAArchiveReaderHostRef>(
                        request.archive_reader_host_ref);
                    ARA::ARAPersistentID id =
                        proxy->host_instance_->archivingControllerInterface
                            ->getDocumentArchiveID(
                                proxy->host_instance_->archivingControllerHostRef,
                                reader);
                    return {id ? std::string(id) : std::string{}};
                },
                [&](const YaAra::HostCallback::IsMusicalContextContentAvailable&
                        request)
                    -> YaAra::HostCallback::IsMusicalContextContentAvailable::
                        Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return {0};
                    if (!proxy->host_instance_ ||
                        !proxy->host_instance_->contentAccessControllerInterface)
                        return {0};
                    ARA::ARAMusicalContextHostRef ctx{};
                    {
                        std::lock_guard ref_lock(proxy->host_refs_mutex_);
                        auto ctx_it = proxy->musical_context_host_refs_.find(
                            request.musical_context_host_ref);
                        if (ctx_it == proxy->musical_context_host_refs_.end())
                            return {0};
                        ctx = ctx_it->second;
                    }
                    return {static_cast<int32_t>(
                        proxy->host_instance_->contentAccessControllerInterface
                            ->isMusicalContextContentAvailable(
                                proxy->host_instance_
                                    ->contentAccessControllerHostRef,
                                ctx,
                                static_cast<ARA::ARAContentType>(
                                    request.content_type)))};
                },
                [&](const YaAra::HostCallback::GetMusicalContextContentGrade&
                        request)
                    -> YaAra::HostCallback::GetMusicalContextContentGrade::
                        Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return {0};
                    if (!proxy->host_instance_ ||
                        !proxy->host_instance_->contentAccessControllerInterface)
                        return {0};
                    ARA::ARAMusicalContextHostRef ctx{};
                    {
                        std::lock_guard ref_lock(proxy->host_refs_mutex_);
                        auto ctx_it = proxy->musical_context_host_refs_.find(
                            request.musical_context_host_ref);
                        if (ctx_it == proxy->musical_context_host_refs_.end())
                            return {0};
                        ctx = ctx_it->second;
                    }
                    return {static_cast<int32_t>(
                        proxy->host_instance_->contentAccessControllerInterface
                            ->getMusicalContextContentGrade(
                                proxy->host_instance_
                                    ->contentAccessControllerHostRef,
                                ctx,
                                static_cast<ARA::ARAContentType>(
                                    request.content_type)))};
                },
                [&](const YaAra::HostCallback::CreateMusicalContextContentReader&
                        request)
                    -> YaAra::HostCallback::CreateMusicalContextContentReader::
                        Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return {0};
                    if (!proxy->host_instance_ ||
                        !proxy->host_instance_->contentAccessControllerInterface)
                        return {0};
                    ARA::ARAMusicalContextHostRef ctx{};
                    {
                        std::lock_guard ref_lock(proxy->host_refs_mutex_);
                        auto ctx_it = proxy->musical_context_host_refs_.find(
                            request.musical_context_host_ref);
                        if (ctx_it == proxy->musical_context_host_refs_.end())
                            return {0};
                        ctx = ctx_it->second;
                    }
                    ARA::ARAContentTimeRange ara_range{};
                    const ARA::ARAContentTimeRange* range_ptr = nullptr;
                    if (request.range) {
                        ara_range = {request.range->start,
                                     request.range->duration};
                        range_ptr = &ara_range;
                    }
                    ARA::ARAContentReaderHostRef reader =
                        proxy->host_instance_->contentAccessControllerInterface
                            ->createMusicalContextContentReader(
                                proxy->host_instance_
                                    ->contentAccessControllerHostRef,
                                ctx,
                                static_cast<ARA::ARAContentType>(
                                    request.content_type),
                                range_ptr);
                    const uint64_t handle =
                        proxy->next_content_reader_handle_.fetch_add(1);
                    proxy->content_reader_host_refs_.emplace(handle, reader);
                    return {handle};
                },
                [&](const YaAra::HostCallback::IsAudioSourceContentAvailable&
                        request)
                    -> YaAra::HostCallback::IsAudioSourceContentAvailable::
                        Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return {0};
                    if (!proxy->host_instance_ ||
                        !proxy->host_instance_->contentAccessControllerInterface)
                        return {0};
                    ARA::ARAAudioSourceHostRef src{};
                    {
                        std::lock_guard ref_lock(proxy->host_refs_mutex_);
                        auto src_it = proxy->audio_source_host_refs_.find(
                            request.audio_source_host_ref);
                        if (src_it == proxy->audio_source_host_refs_.end())
                            return {0};
                        src = src_it->second;
                    }
                    return {static_cast<int32_t>(
                        proxy->host_instance_->contentAccessControllerInterface
                            ->isAudioSourceContentAvailable(
                                proxy->host_instance_
                                    ->contentAccessControllerHostRef,
                                src,
                                static_cast<ARA::ARAContentType>(
                                    request.content_type)))};
                },
                [&](const YaAra::HostCallback::GetAudioSourceContentGrade&
                        request)
                    -> YaAra::HostCallback::GetAudioSourceContentGrade::
                        Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return {0};
                    if (!proxy->host_instance_ ||
                        !proxy->host_instance_->contentAccessControllerInterface)
                        return {0};
                    ARA::ARAAudioSourceHostRef src{};
                    {
                        std::lock_guard ref_lock(proxy->host_refs_mutex_);
                        auto src_it = proxy->audio_source_host_refs_.find(
                            request.audio_source_host_ref);
                        if (src_it == proxy->audio_source_host_refs_.end())
                            return {0};
                        src = src_it->second;
                    }
                    return {static_cast<int32_t>(
                        proxy->host_instance_->contentAccessControllerInterface
                            ->getAudioSourceContentGrade(
                                proxy->host_instance_
                                    ->contentAccessControllerHostRef,
                                src,
                                static_cast<ARA::ARAContentType>(
                                    request.content_type)))};
                },
                [&](const YaAra::HostCallback::CreateAudioSourceContentReader&
                        request)
                    -> YaAra::HostCallback::CreateAudioSourceContentReader::
                        Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return {0};
                    if (!proxy->host_instance_ ||
                        !proxy->host_instance_->contentAccessControllerInterface)
                        return {0};
                    ARA::ARAAudioSourceHostRef src_ref{};
                    {
                        std::lock_guard ref_lock(proxy->host_refs_mutex_);
                        auto src_it = proxy->audio_source_host_refs_.find(
                            request.audio_source_host_ref);
                        if (src_it == proxy->audio_source_host_refs_.end())
                            return {0};
                        src_ref = src_it->second;
                    }
                    ARA::ARAContentTimeRange ara_range{};
                    const ARA::ARAContentTimeRange* range_ptr = nullptr;
                    if (request.range) {
                        ara_range = {request.range->start,
                                     request.range->duration};
                        range_ptr = &ara_range;
                    }
                    ARA::ARAContentReaderHostRef reader =
                        proxy->host_instance_->contentAccessControllerInterface
                            ->createAudioSourceContentReader(
                                proxy->host_instance_
                                    ->contentAccessControllerHostRef,
                                src_ref,
                                static_cast<ARA::ARAContentType>(
                                    request.content_type),
                                range_ptr);
                    const uint64_t handle =
                        proxy->next_content_reader_handle_.fetch_add(1);
                    proxy->content_reader_host_refs_.emplace(handle, reader);
                    return {handle};
                },
                [&](const YaAra::HostCallback::GetContentReaderEventCount&
                        request)
                    -> YaAra::HostCallback::GetContentReaderEventCount::
                        Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return {0};
                    if (!proxy->host_instance_ ||
                        !proxy->host_instance_->contentAccessControllerInterface)
                        return {0};
                    auto r_it = proxy->content_reader_host_refs_.find(
                        request.content_reader_host_ref);
                    if (r_it == proxy->content_reader_host_refs_.end())
                        return {0};
                    return {proxy->host_instance_
                                ->contentAccessControllerInterface
                                ->getContentReaderEventCount(
                                    proxy->host_instance_
                                        ->contentAccessControllerHostRef,
                                    r_it->second)};
                },
                [&](const YaAra::HostCallback::GetContentReaderDataForEvent&
                        request)
                    -> YaAra::HostCallback::GetContentReaderDataForEvent::
                        Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return {{}};
                    if (!proxy->host_instance_ ||
                        !proxy->host_instance_->contentAccessControllerInterface)
                        return {{}};
                    auto r_it = proxy->content_reader_host_refs_.find(
                        request.content_reader_host_ref);
                    if (r_it == proxy->content_reader_host_refs_.end())
                        return {{}};
                    const void* data =
                        proxy->host_instance_->contentAccessControllerInterface
                            ->getContentReaderDataForEvent(
                                proxy->host_instance_
                                    ->contentAccessControllerHostRef,
                                r_it->second,
                                request.event_index);
                    if (!data)
                        return {{}};
                    switch (static_cast<ARA::ARAContentType>(
                        request.content_type)) {
                        case ARA::kARAContentTypeNotes: {
                            const auto* e =
                                static_cast<const ARA::ARAContentNote*>(data);
                            std::vector<uint8_t> out(sizeof(*e));
                            std::memcpy(out.data(), e, sizeof(*e));
                            return {std::move(out)};
                        }
                        case ARA::kARAContentTypeTempoEntries: {
                            const auto* e =
                                static_cast<const ARA::ARAContentTempoEntry*>(data);
                            std::vector<uint8_t> out(sizeof(*e));
                            std::memcpy(out.data(), e, sizeof(*e));
                            return {std::move(out)};
                        }
                        case ARA::kARAContentTypeBarSignatures: {
                            const auto* e =
                                static_cast<const ARA::ARAContentBarSignature*>(data);
                            std::vector<uint8_t> out(sizeof(*e));
                            std::memcpy(out.data(), e, sizeof(*e));
                            return {std::move(out)};
                        }
                        case ARA::kARAContentTypeStaticTuning: {
                            const auto* e =
                                static_cast<const ARA::ARAContentTuning*>(data);
                            const std::string name =
                                e->name ? std::string(e->name) : std::string{};
                            const uint32_t name_len =
                                static_cast<uint32_t>(name.size());
                            std::vector<uint8_t> out;
                            out.reserve(sizeof(e->concertPitchFrequency) +
                                        sizeof(e->tunings) +
                                        sizeof(name_len) + name_len);
                            auto push = [&](const void* p, size_t n) {
                                const auto* b = static_cast<const uint8_t*>(p);
                                out.insert(out.end(), b, b + n);
                            };
                            push(&e->concertPitchFrequency,
                                 sizeof(e->concertPitchFrequency));
                            push(&e->tunings,
                                 sizeof(e->tunings));
                            push(&name_len, sizeof(name_len));
                            out.insert(out.end(), name.begin(), name.end());
                            return {std::move(out)};
                        }
                        case ARA::kARAContentTypeKeySignatures: {
                            const auto* e =
                                static_cast<const ARA::ARAContentKeySignature*>(
                                    data);
                            const std::string name =
                                e->name ? std::string(e->name) : std::string{};
                            const uint32_t name_len =
                                static_cast<uint32_t>(name.size());
                            std::vector<uint8_t> out;
                            out.reserve(sizeof(e->root) +
                                        sizeof(e->intervals) +
                                        sizeof(name_len) + name_len +
                                        sizeof(e->position));
                            auto push = [&](const void* p, size_t n) {
                                const auto* b = static_cast<const uint8_t*>(p);
                                out.insert(out.end(), b, b + n);
                            };
                            push(&e->root, sizeof(e->root));
                            push(&e->intervals, sizeof(e->intervals));
                            push(&name_len, sizeof(name_len));
                            out.insert(out.end(), name.begin(), name.end());
                            push(&e->position, sizeof(e->position));
                            return {std::move(out)};
                        }
                        case ARA::kARAContentTypeSheetChords: {
                            const auto* e =
                                static_cast<const ARA::ARAContentChord*>(data);
                            const std::string name =
                                e->name ? std::string(e->name) : std::string{};
                            const uint32_t name_len =
                                static_cast<uint32_t>(name.size());
                            std::vector<uint8_t> out;
                            out.reserve(
                                sizeof(e->root) + sizeof(e->bass) +
                                sizeof(e->intervals) +
                                sizeof(name_len) + name_len +
                                sizeof(e->position));
                            auto push = [&](const void* p, size_t n) {
                                const auto* b =
                                    static_cast<const uint8_t*>(p);
                                out.insert(out.end(), b, b + n);
                            };
                            push(&e->root, sizeof(e->root));
                            push(&e->bass, sizeof(e->bass));
                            push(&e->intervals, sizeof(e->intervals));
                            push(&name_len, sizeof(name_len));
                            out.insert(out.end(),
                                       name.begin(), name.end());
                            push(&e->position, sizeof(e->position));
                            return {std::move(out)};
                        }
                        default:
                            return {{}};
                    }
                },
                [&](const YaAra::HostCallback::DestroyContentReader& request)
                    -> YaAra::HostCallback::DestroyContentReader::Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return Ack{};
                    auto it = proxy->content_reader_host_refs_.find(
                        request.content_reader_host_ref);
                    if (it != proxy->content_reader_host_refs_.end()) {
                        if (proxy->host_instance_ &&
                            proxy->host_instance_
                                ->contentAccessControllerInterface) {
                            proxy->host_instance_
                                ->contentAccessControllerInterface
                                ->destroyContentReader(
                                    proxy->host_instance_
                                        ->contentAccessControllerHostRef,
                                    it->second);
                        }
                        proxy->content_reader_host_refs_.erase(it);
                    }
                    return Ack{};
                },
                [&](const YaAra::HostCallback::NotifyAudioSourceAnalysisProgress&
                        request)
                    -> YaAra::HostCallback::NotifyAudioSourceAnalysisProgress::
                        Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return Ack{};
                    if (!proxy->host_instance_ ||
                        !proxy->host_instance_->modelUpdateControllerInterface)
                        return Ack{};
                    std::lock_guard ref_lock(proxy->host_refs_mutex_);
                    auto src_it = proxy->audio_source_host_refs_.find(
                        request.audio_source_host_ref);
                    if (src_it == proxy->audio_source_host_refs_.end())
                        return Ack{};
                    proxy->host_instance_->modelUpdateControllerInterface
                        ->notifyAudioSourceAnalysisProgress(
                            proxy->host_instance_->modelUpdateControllerHostRef,
                            src_it->second,
                            static_cast<ARA::ARAAnalysisProgressState>(
                                request.state),
                            request.value);
                    return Ack{};
                },
                [&](const YaAra::HostCallback::NotifyAudioSourceContentChanged&
                        request)
                    -> YaAra::HostCallback::NotifyAudioSourceContentChanged::
                        Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return Ack{};
                    if (!proxy->host_instance_ ||
                        !proxy->host_instance_->modelUpdateControllerInterface)
                        return Ack{};
                    std::lock_guard ref_lock(proxy->host_refs_mutex_);
                    auto src_it = proxy->audio_source_host_refs_.find(
                        request.audio_source_host_ref);
                    if (src_it == proxy->audio_source_host_refs_.end())
                        return Ack{};
                    ARA::ARAContentTimeRange ara_range{};
                    const ARA::ARAContentTimeRange* range_ptr = nullptr;
                    if (request.range) {
                        ara_range = {request.range->start,
                                     request.range->duration};
                        range_ptr = &ara_range;
                    }
                    proxy->host_instance_->modelUpdateControllerInterface
                        ->notifyAudioSourceContentChanged(
                            proxy->host_instance_->modelUpdateControllerHostRef,
                            src_it->second,
                            range_ptr,
                            static_cast<ARA::ARAContentUpdateFlags>(
                                request.flags));
                    return Ack{};
                },
                [&](const YaAra::HostCallback::
                        NotifyAudioModificationContentChanged& request)
                    -> YaAra::HostCallback::
                        NotifyAudioModificationContentChanged::Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return Ack{};
                    if (!proxy->host_instance_ ||
                        !proxy->host_instance_->modelUpdateControllerInterface)
                        return Ack{};
                    std::lock_guard ref_lock(proxy->host_refs_mutex_);
                    auto mod_it = proxy->audio_modification_host_refs_.find(
                        request.audio_modification_host_ref);
                    if (mod_it == proxy->audio_modification_host_refs_.end())
                        return Ack{};
                    ARA::ARAContentTimeRange ara_range{};
                    const ARA::ARAContentTimeRange* range_ptr = nullptr;
                    if (request.range) {
                        ara_range = {request.range->start,
                                     request.range->duration};
                        range_ptr = &ara_range;
                    }
                    proxy->host_instance_->modelUpdateControllerInterface
                        ->notifyAudioModificationContentChanged(
                            proxy->host_instance_->modelUpdateControllerHostRef,
                            mod_it->second,
                            range_ptr,
                            static_cast<ARA::ARAContentUpdateFlags>(
                                request.flags));
                    return Ack{};
                },
                [&](const YaAra::HostCallback::
                        NotifyPlaybackRegionContentChanged& request)
                    -> YaAra::HostCallback::
                        NotifyPlaybackRegionContentChanged::Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return Ack{};
                    if (!proxy->host_instance_ ||
                        !proxy->host_instance_->modelUpdateControllerInterface)
                        return Ack{};
                    std::lock_guard ref_lock(proxy->host_refs_mutex_);
                    auto reg_it = proxy->playback_region_host_refs_.find(
                        request.playback_region_host_ref);
                    if (reg_it == proxy->playback_region_host_refs_.end())
                        return Ack{};
                    ARA::ARAContentTimeRange ara_range{};
                    const ARA::ARAContentTimeRange* range_ptr = nullptr;
                    if (request.range) {
                        ara_range = {request.range->start,
                                     request.range->duration};
                        range_ptr = &ara_range;
                    }
                    proxy->host_instance_->modelUpdateControllerInterface
                        ->notifyPlaybackRegionContentChanged(
                            proxy->host_instance_->modelUpdateControllerHostRef,
                            reg_it->second,
                            range_ptr,
                            static_cast<ARA::ARAContentUpdateFlags>(
                                request.flags));
                    return Ack{};
                },
                [&](const YaAra::HostCallback::NotifyDocumentDataChanged&
                        request)
                    -> YaAra::HostCallback::NotifyDocumentDataChanged::
                        Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return Ack{};
                    const auto* iface =
                        proxy->host_instance_
                            ? proxy->host_instance_
                                  ->modelUpdateControllerInterface
                            : nullptr;
                    if (iface &&
                        iface->structSize >=
                            ARA_IMPLEMENTED_STRUCT_SIZE(
                                ARAModelUpdateControllerInterface,
                                notifyDocumentDataChanged) &&
                        iface->notifyDocumentDataChanged)
                        iface->notifyDocumentDataChanged(
                            proxy->host_instance_
                                ->modelUpdateControllerHostRef);
                    return Ack{};
                },
                [&](const YaAra::HostCallback::RequestStartPlayback& request)
                    -> YaAra::HostCallback::RequestStartPlayback::Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return Ack{};
                    if (proxy->host_instance_ &&
                        proxy->host_instance_->playbackControllerInterface)
                        proxy->host_instance_->playbackControllerInterface
                            ->requestStartPlayback(
                                proxy->host_instance_->playbackControllerHostRef);
                    return Ack{};
                },
                [&](const YaAra::HostCallback::RequestStopPlayback& request)
                    -> YaAra::HostCallback::RequestStopPlayback::Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return Ack{};
                    if (proxy->host_instance_ &&
                        proxy->host_instance_->playbackControllerInterface)
                        proxy->host_instance_->playbackControllerInterface
                            ->requestStopPlayback(
                                proxy->host_instance_->playbackControllerHostRef);
                    return Ack{};
                },
                [&](const YaAra::HostCallback::RequestSetPlaybackPosition&
                        request)
                    -> YaAra::HostCallback::RequestSetPlaybackPosition::
                        Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return Ack{};
                    if (proxy->host_instance_ &&
                        proxy->host_instance_->playbackControllerInterface)
                        proxy->host_instance_->playbackControllerInterface
                            ->requestSetPlaybackPosition(
                                proxy->host_instance_->playbackControllerHostRef,
                                request.time_position);
                    return Ack{};
                },
                [&](const YaAra::HostCallback::RequestSetCycleRange& request)
                    -> YaAra::HostCallback::RequestSetCycleRange::Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return Ack{};
                    if (proxy->host_instance_ &&
                        proxy->host_instance_->playbackControllerInterface)
                        proxy->host_instance_->playbackControllerInterface
                            ->requestSetCycleRange(
                            proxy->host_instance_->playbackControllerHostRef,
                            request.start_time,
                            request.duration);
                    return Ack{};
                },
                [&](const YaAra::HostCallback::RequestEnableCycle& request)
                    -> YaAra::HostCallback::RequestEnableCycle::Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return Ack{};
                    if (proxy->host_instance_ &&
                        proxy->host_instance_->playbackControllerInterface)
                        proxy->host_instance_->playbackControllerInterface
                            ->requestEnableCycle(
                                proxy->host_instance_->playbackControllerHostRef,
                                static_cast<ARA::ARABool>(request.enable));
                    return Ack{};
                },
                [&](const YaAra::CreateAudioReader& request)
                    -> YaAra::CreateAudioReader::Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy || !proxy->host_instance_ ||
                        !proxy->host_instance_->audioAccessControllerInterface)
                        return {};
                    ARA::ARAAudioSourceHostRef host_ref = nullptr;
                    int32_t channel_count = 0;
                    {
                        std::lock_guard ref_lock(proxy->host_refs_mutex_);
                        auto src_it = proxy->audio_source_host_refs_.find(
                            request.audio_source_host_ref);
                        if (src_it == proxy->audio_source_host_refs_.end())
                            return {};
                        host_ref = src_it->second;
                        auto cc_it = proxy->audio_source_channel_counts_.find(
                            request.audio_source_host_ref);
                        channel_count =
                            cc_it != proxy->audio_source_channel_counts_.end()
                                ? cc_it->second
                                : 0;
                    }
                    if (channel_count <= 0 || channel_count > 8191)
                        return {};
                    const uint32_t bytes_per_sample =
                        static_cast<uint32_t>(request.use_64bit ? sizeof(double)
                                                                 : sizeof(float));
                    const uint32_t max_block =
                        static_cast<uint32_t>(65536) * bytes_per_sample;
                    if (static_cast<uint64_t>(channel_count) * max_block >
                        std::numeric_limits<uint32_t>::max())
                        return {};
                    ARA::ARAAudioReaderHostRef reader =
                        proxy->host_instance_->audioAccessControllerInterface
                            ->createAudioReaderForSource(
                                proxy->host_instance_->audioAccessControllerHostRef,
                                host_ref,
                                static_cast<ARA::ARABool>(request.use_64bit));
                    if (!reader) {
                        logger_.log(
                            "WARNING: ARA createAudioReaderForSource returned "
                            "null for src=" +
                            std::to_string(request.audio_source_host_ref));
                        return {};
                    }
                    const uint64_t reader_id =
                        proxy->next_audio_reader_handle_.fetch_add(1);
                    {
                        std::lock_guard ref_lock(proxy->host_refs_mutex_);
                        proxy->audio_reader_host_refs_.emplace(reader_id, reader);
                    }
                    AudioShmBuffer::Config cfg{};
                    cfg.name = "yabridge-ara-" + std::to_string(reader_id);
                    cfg.size = static_cast<uint32_t>(channel_count) * max_block;
                    std::vector<uint32_t> offsets(
                        static_cast<size_t>(channel_count));
                    for (int32_t c = 0; c < channel_count; ++c)
                        offsets[static_cast<size_t>(c)] =
                            static_cast<uint32_t>(c) * max_block;
                    cfg.input_offsets = {offsets};
                    cfg.output_offsets = {};
                    const uint32_t max_samples = max_block / bytes_per_sample;
                    {
                        std::lock_guard shm_lock(
                            proxy->audio_reader_shm_mutex_);
                        proxy->audio_reader_shm_buffers_.emplace(
                            reader_id, std::make_shared<AudioShmBuffer>(cfg));
                        proxy->audio_reader_max_samples_.emplace(
                            reader_id, max_samples);
                    }
                    return {reader_id, std::move(cfg)};
                },
                [&](const YaAra::DestroyAudioReader& request)
                    -> YaAra::DestroyAudioReader::Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return Ack{};
                    ARA::ARAAudioReaderHostRef reader = nullptr;
                    {
                        std::lock_guard ref_lock(proxy->host_refs_mutex_);
                        auto it = proxy->audio_reader_host_refs_.find(
                            request.audio_reader_id);
                        if (it != proxy->audio_reader_host_refs_.end()) {
                            reader = it->second;
                            proxy->audio_reader_host_refs_.erase(it);
                        }
                    }
                    if (reader && proxy->host_instance_ &&
                        proxy->host_instance_->audioAccessControllerInterface)
                        proxy->host_instance_->audioAccessControllerInterface
                            ->destroyAudioReader(
                                proxy->host_instance_->audioAccessControllerHostRef,
                                reader);
                    {
                        std::lock_guard shm_lock(
                            proxy->audio_reader_shm_mutex_);
                        proxy->audio_reader_shm_buffers_.erase(
                            request.audio_reader_id);
                        proxy->audio_reader_max_samples_.erase(
                            request.audio_reader_id);
                    }
                    return Ack{};
                },
                [&](const YaAra::ReadAudioSamples& request)
                    -> YaAra::ReadAudioSamples::Response {
                    auto proxy = resolve_proxy(request.ara_dc_id);
                    if (!proxy)
                        return {false};
                    ARA::ARAAudioReaderHostRef reader = nullptr;
                    {
                        std::lock_guard ref_lock(proxy->host_refs_mutex_);
                        auto it = proxy->audio_reader_host_refs_.find(
                            request.audio_reader_host_ref);
                        if (it == proxy->audio_reader_host_refs_.end())
                            return {false};
                        reader = it->second;
                    }
                    std::shared_ptr<AudioShmBuffer> shm;
                    int32_t channel_count = 0;
                    {
                        std::lock_guard shm_lock(
                            proxy->audio_reader_shm_mutex_);
                        auto it = proxy->audio_reader_shm_buffers_.find(
                            request.audio_reader_host_ref);
                        if (it == proxy->audio_reader_shm_buffers_.end())
                            return {false};
                        shm = it->second;
                        channel_count = static_cast<int32_t>(
                            shm->config_.input_offsets.empty()
                                ? 0
                                : shm->config_.input_offsets[0].size());
                        auto ms_it = proxy->audio_reader_max_samples_.find(
                            request.audio_reader_host_ref);
                        const uint32_t max_samples =
                            ms_it != proxy->audio_reader_max_samples_.end()
                                ? ms_it->second
                                : 0;
                        if (request.sample_count <= 0 ||
                            static_cast<uint32_t>(request.sample_count) >
                                max_samples)
                            return {false};
                    }
                    const size_t bytes_per_sample =
                        request.use_64bit ? sizeof(double) : sizeof(float);
                    const size_t bytes_per_channel =
                        static_cast<size_t>(request.sample_count) *
                        bytes_per_sample;
                    if (!proxy->host_instance_ ||
                        !proxy->host_instance_->audioAccessControllerInterface) {
                        for (int32_t c = 0; c < channel_count; ++c) {
                            void* dst = request.use_64bit
                                ? static_cast<void*>(shm->input_channel_ptr<double>(
                                      0, static_cast<uint32_t>(c)))
                                : static_cast<void*>(shm->input_channel_ptr<float>(
                                      0, static_cast<uint32_t>(c)));
                            std::memset(dst, 0, bytes_per_channel);
                        }
                        return {false};
                    }
                    std::vector<void*> buffers(
                        static_cast<size_t>(channel_count));
                    for (int32_t c = 0; c < channel_count; ++c) {
                        buffers[static_cast<size_t>(c)] =
                            request.use_64bit
                                ? static_cast<void*>(shm->input_channel_ptr<double>(
                                      0, static_cast<uint32_t>(c)))
                                : static_cast<void*>(shm->input_channel_ptr<float>(
                                      0, static_cast<uint32_t>(c)));
                    }
                    const ARA::ARABool ok =
                        proxy->host_instance_->audioAccessControllerInterface
                            ->readAudioSamples(
                                proxy->host_instance_
                                    ->audioAccessControllerHostRef,
                                reader,
                                static_cast<ARA::ARASamplePosition>(
                                    request.sample_position),
                                static_cast<ARA::ARASampleCount>(
                                    request.sample_count),
                                buffers.data());
                    if (!ok) {
                        for (int32_t c = 0; c < channel_count; ++c)
                            std::memset(buffers[static_cast<size_t>(c)], 0,
                                        bytes_per_channel);
                    }
                    return {ok != ARA::kARAFalse};
                },
#endif  // WITH_ARA
            });
    });
}

Vst3PluginBridge::~Vst3PluginBridge() noexcept {
    try {
        // Drop all work make sure all sockets are closed
        plugin_host_->terminate();
        io_context_.stop();
    } catch (const std::system_error&) {
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
            sockets_.host_plugin_control_.send_message(
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

#ifdef WITH_ARA

const ARA::ARADocumentControllerInstance*
Vst3PluginBridge::register_ara_document_controller(
    native_size_t ara_dc_id,
    const ARA::ARADocumentControllerHostInstance* host_instance,
    const ARA::ARAFactory* factory) {
    auto proxy = std::make_shared<AraDocumentControllerProxy>(*this, ara_dc_id);
    proxy->host_instance_ = host_instance;
    proxy->factory_ = factory;

    std::lock_guard lock(ara_document_controllers_mutex_);
    // Use operator[] so a stale entry from a removed+re-added plugin is
    // silently replaced rather than rejected.
    auto& slot = ara_document_controllers_[ara_dc_id];
    slot = std::move(proxy);
    return &slot->ara_dc_instance();
}

void Vst3PluginBridge::unregister_ara_document_controller(
    native_size_t ara_dc_id) {
    std::lock_guard lock(ara_document_controllers_mutex_);
    if (ara_document_controllers_.erase(ara_dc_id) == 0) {
        logger_.log("WARNING: unregister_ara_document_controller() called with "
                    "unknown ara_dc_id");
    }
}

std::shared_ptr<AraDocumentControllerProxy>
Vst3PluginBridge::find_ara_document_controller(
    ARA::ARADocumentControllerRef ref) noexcept {
    std::lock_guard lock(ara_document_controllers_mutex_);
    for (const auto& [id, ptr] : ara_document_controllers_) {
        if (ptr.get() ==
            reinterpret_cast<AraDocumentControllerProxy*>(ref))
            return ptr;
    }
    return nullptr;
}

#endif  // WITH_ARA
