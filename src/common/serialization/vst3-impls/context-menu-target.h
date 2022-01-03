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

#pragma once

#include "../vst3/context-menu-target.h"

/**
 * This implementation used to live in `src/plugin/bridges/vst3-impls`, but
 * since plugins can also call context menu items added by the host this is
 * needed on both sides.
 *
 * NOTE: Bitwig does not actually set the tags here, so host menu items need to
 *       be identified through their item ID, not through the tag.
 */
template <typename Bridge>
class YaContextMenuTargetImpl : public YaContextMenuTarget {
   public:
    YaContextMenuTargetImpl(Bridge& bridge, ConstructArgs&& args) noexcept
        : YaContextMenuTarget(std::move(args)), bridge_(bridge) {}

    /**
     * We'll override the query interface to log queries for interfaces we do
     * not (yet) support.
     */
    tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                      void** obj) override {
        const tresult result = YaContextMenuTarget::queryInterface(_iid, obj);
        bridge_.logger_.log_query_interface(
            "In IContextMenuTarget::queryInterface()", result,
            Steinberg::FUID::fromTUID(_iid));

        return result;
    }

    // From `IContextMenuTarget`
    tresult PLUGIN_API executeMenuItem(int32 tag) override {
        // NOTE: This requires mutual recursion, because REAPER will call
        //       `getState()` whle the context menu is open, and `getState()`
        //       also has to be handled from the GUI thread
        return bridge_.send_mutually_recursive_message(
            YaContextMenuTarget::ExecuteMenuItem{
                .owner_instance_id = owner_instance_id(),
                .context_menu_id = context_menu_id(),
                .item_id = item_id(),
                .target_tag = target_tag(),
                .tag = tag});
    }

   private:
    Bridge& bridge_;
};
