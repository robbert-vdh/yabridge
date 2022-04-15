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

#include "context-menu-proxy.h"

#include <iostream>

#include "../../common/serialization/vst3-impls/context-menu-target.h"

Vst3ContextMenuProxyImpl::Vst3ContextMenuProxyImpl(
    Vst3Bridge& bridge,
    Vst3ContextMenuProxy::ConstructArgs&& args)
    : Vst3ContextMenuProxy(std::move(args)),
      bridge_(bridge),
      items_(std::move(YaContextMenu::arguments_.items)) {
    bridge.register_context_menu(*this);

    // The host has likely prepopulated the context menu with its own items. In
    // that case we should create proxy targets for those so the plugin can call
    // those menu items.
    const int32 num_items = static_cast<int32>(items_.size());
    for (int32 item_idx = 0; item_idx < num_items; item_idx++) {
        auto& item = items_[item_idx];

        // NOTE: These host targets are indexed by the item's index because
        //       Bitwig doesn't assign tags to their own menu items
        host_targets_[item_idx] = Steinberg::owned(new YaContextMenuTargetImpl(
            bridge, YaContextMenuTarget::ConstructArgs{
                        .owner_instance_id = owner_instance_id(),
                        .context_menu_id = context_menu_id(),
                        .item_id = static_cast<int32>(item_idx),
                        .tag = item.tag}));
    }
}

Vst3ContextMenuProxyImpl::~Vst3ContextMenuProxyImpl() noexcept {
    // Also drop the context menu smart pointer on plugin side when this gets
    // dropped
    // NOTE: This can actually throw (e.g. out of memory or the socket got
    //       closed). But if that were to happen, then we wouldn't be able to
    //       recover from it anyways.
    bridge_.send_message(
        Vst3ContextMenuProxy::Destruct{.owner_instance_id = owner_instance_id(),
                                       .context_menu_id = context_menu_id()});
    bridge_.unregister_context_menu(*this);
}

tresult PLUGIN_API
Vst3ContextMenuProxyImpl::queryInterface(const Steinberg::TUID _iid,
                                         void** obj) {
    const tresult result = Vst3ContextMenuProxy::queryInterface(_iid, obj);
    bridge_.logger_.log_query_interface("In IContextMenu::queryInterface()",
                                        result,
                                        Steinberg::FUID::fromTUID(_iid));

    return result;
}

int32 PLUGIN_API Vst3ContextMenuProxyImpl::getItemCount() {
    return static_cast<int32>(items_.size());
}

tresult PLUGIN_API Vst3ContextMenuProxyImpl::getItem(
    int32 index,
    Steinberg::Vst::IContextMenuItem& item /*out*/,
    Steinberg::Vst::IContextMenuTarget** target /*out*/) {
    // XXX: Should the plugin be able to get targets created by the host this
    //      way? We'll just assume that this function won't ever be called by
    //      the plugin (but we'll implement a basic version anyways).
    if (index < 0 || index >= static_cast<int32>(items_.size())) {
        return Steinberg::kInvalidArgument;
    }

    item = items_[index];
    if (target) {
        // The item is either a context menu item prepopulated by the host or an
        // item created by the plugin itself
        if (auto plugin_target = plugin_targets_.find(item.tag);
            plugin_target != plugin_targets_.end()) {
            *target = plugin_target->second;
            return Steinberg::kResultOk;
        } else if (auto proxy_target = host_targets_.find(index);
                   // NOTE: These proxy targets are indexed by the item's index
                   //       because Bitwig doesn't assign tags to their context
                   //       menu items
                   proxy_target != host_targets_.end()) {
            *target = proxy_target->second;
            return Steinberg::kResultOk;
        } else {
            *target = nullptr;
            return Steinberg::kResultFalse;
        }
    } else {
        std::cerr << "WARNING: Null pointer passed to 'IContextMenu::getItem()'"
                  << std::endl;
        return Steinberg::kInvalidArgument;
    }
}

tresult PLUGIN_API
Vst3ContextMenuProxyImpl::addItem(const Steinberg::Vst::IContextMenuItem& item,
                                  Steinberg::Vst::IContextMenuTarget* target) {
    // TODO: I haven't come across a plugin that adds its own items to the
    //       host's context menu, so this hasn't been tested yet
    const tresult result = bridge_.send_message(YaContextMenu::AddItem{
        .owner_instance_id = owner_instance_id(),
        .context_menu_id = context_menu_id(),
        .item = item,
        .target = (target ? std::optional(YaContextMenuTarget::ConstructArgs{
                                .owner_instance_id = owner_instance_id(),
                                .context_menu_id = context_menu_id(),
                                // This item ID isn't actually used here because
                                // it's only needed to work around a Bitwig bug
                                // when calling host menu items from a plugin
                                .item_id = static_cast<int32>(items_.size()),
                                .tag = item.tag})
                          : std::nullopt)});

    if (result == Steinberg::kResultOk) {
        items_.push_back(item);
        plugin_targets_[item.tag] = target;
    }

    return result;
}

tresult PLUGIN_API Vst3ContextMenuProxyImpl::removeItem(
    const Steinberg::Vst::IContextMenuItem& item,
    Steinberg::Vst::IContextMenuTarget* /*target*/) {
    const tresult result = bridge_.send_message(
        YaContextMenu::RemoveItem{.owner_instance_id = owner_instance_id(),
                                  .context_menu_id = context_menu_id(),
                                  .item = item});

    if (result == Steinberg::kResultOk) {
        items_.erase(
            std::remove_if(
                items_.begin(), items_.end(),
                [&](const Steinberg::Vst::IContextMenuItem& candidate_item) {
                    // They didn't implement `operator==` on the struct
                    return candidate_item.tag == item.tag;
                }),
            items_.end());

        // The target can be either a proxy target or a target added by the
        // plugin
        if (plugin_targets_.erase(item.tag) == 0) {
            host_targets_.erase(item.tag);
        }
    }

    return result;
}

tresult PLUGIN_API Vst3ContextMenuProxyImpl::popup(Steinberg::UCoord x,
                                                   Steinberg::UCoord y) {
    // NOTE: This requires mutual recursion, because REAPER will call
    //       `getState()` whle the context menu is open, and `getState()` also
    //       has to be handled from the GUI thread
    return bridge_.send_mutually_recursive_message(
        YaContextMenu::Popup{.owner_instance_id = owner_instance_id(),
                             .context_menu_id = context_menu_id(),
                             .x = x,
                             .y = y});
}
