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

#include "context-menu-proxy.h"

#include <iostream>

Vst3ContextMenuProxyImpl::Vst3ContextMenuProxyImpl(
    Vst3Bridge& bridge,
    Vst3ContextMenuProxy::ConstructArgs&& args) noexcept
    : Vst3ContextMenuProxy(std::move(args)), bridge(bridge) {
    bridge.register_context_menu(*this);
}

Vst3ContextMenuProxyImpl::~Vst3ContextMenuProxyImpl() noexcept {
    // Also drop the context menu smart pointer on plugin side when this gets
    // dropped
    // NOTE: This can actually throw (e.g. out of memory or the socket got
    //       closed). But if that were to happen, then we wouldn't be able to
    //       recover from it anyways.
    bridge.send_message(
        Vst3ContextMenuProxy::Destruct{.owner_instance_id = owner_instance_id(),
                                       .context_menu_id = context_menu_id()});
    bridge.unregister_context_menu(*this);
}

tresult PLUGIN_API
Vst3ContextMenuProxyImpl::queryInterface(const Steinberg::TUID _iid,
                                         void** obj) {
    const tresult result = Vst3ContextMenuProxy::queryInterface(_iid, obj);
    bridge.logger.log_query_interface("In IContextMenu::queryInterface()",
                                      result, Steinberg::FUID::fromTUID(_iid));

    return result;
}

int32 PLUGIN_API Vst3ContextMenuProxyImpl::getItemCount() {
    return bridge.send_message(
        YaContextMenu::GetItemCount{.owner_instance_id = owner_instance_id(),
                                    .context_menu_id = context_menu_id()});
}

tresult PLUGIN_API Vst3ContextMenuProxyImpl::getItem(
    int32 index,
    Steinberg::Vst::IContextMenuItem& item /*out*/,
    Steinberg::Vst::IContextMenuTarget** target /*out*/) {
    // XXX: Should the plugin be able to get targets created by the host this
    //      way? We'll just assume that this function won't ever be called by
    //      the plugin (but we'll implement a basic version anyways).
    if (index < 0 || index >= static_cast<int32>(items.size())) {
        return Steinberg::kInvalidArgument;
    } else {
        item = items[index];
        *target = context_menu_targets[item.tag];

        return Steinberg::kResultOk;
    }
}

tresult PLUGIN_API
Vst3ContextMenuProxyImpl::addItem(const Steinberg::Vst::IContextMenuItem& item,
                                  Steinberg::Vst::IContextMenuTarget* target) {
    // TODO: I haven't come across a plugin that adds its own items, so this
    //       hasn't been tested yet
    const tresult result = bridge.send_message(YaContextMenu::AddItem{
        .owner_instance_id = owner_instance_id(),
        .context_menu_id = context_menu_id(),
        .item = item,
        .target =
            (target ? std::make_optional<YaContextMenuTarget::ConstructArgs>(
                          owner_instance_id(), context_menu_id(), item.tag)
                    : std::nullopt)});

    if (result == Steinberg::kResultOk) {
        items.push_back(item);
        context_menu_targets[item.tag] = target;
    }

    return result;
}

tresult PLUGIN_API Vst3ContextMenuProxyImpl::removeItem(
    const Steinberg::Vst::IContextMenuItem& item,
    Steinberg::Vst::IContextMenuTarget* /*target*/) {
    const tresult result = bridge.send_message(
        YaContextMenu::RemoveItem{.owner_instance_id = owner_instance_id(),
                                  .context_menu_id = context_menu_id(),
                                  .item = item});

    if (result == Steinberg::kResultOk) {
        items.erase(
            std::remove_if(
                items.begin(), items.end(),
                [&](const Steinberg::Vst::IContextMenuItem& candidate_item) {
                    // They didn't implement `operator==` on the struct
                    return candidate_item.tag == item.tag;
                }),
            items.end());
        context_menu_targets.erase(item.tag);
    }

    return result;
}

tresult PLUGIN_API Vst3ContextMenuProxyImpl::popup(Steinberg::UCoord x,
                                                   Steinberg::UCoord y) {
    // NOTE: This requires mutual recursion, because REAPER will call
    //       `getState()` whle the context menu is open, and `getState()` also
    //       has to be handled from the GUi thread
    return bridge.send_mutually_recursive_message(
        YaContextMenu::Popup{.owner_instance_id = owner_instance_id(),
                             .context_menu_id = context_menu_id(),
                             .x = x,
                             .y = y});
}
