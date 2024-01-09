// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2024 Robbert van der Helm
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

#include <pluginterfaces/vst/ivstcontextmenu.h>

#include "../common.h"
#include "base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IContextMenuTarget` for proxying calls to specific
 * `IContextMenu` items. These are created on the plugin side, and when
 * `executeMenuItem()` gets called we execute the corresponding menu item's
 * target _from the GUI thread_.
 */
class YaContextMenuTarget : public Steinberg::Vst::IContextMenuTarget {
   public:
    /**
     * These are the arguments for constructing a
     * `YaContextMenuTargetImpl`.
     */
    struct ConstructArgs {
        native_size_t owner_instance_id;
        native_size_t context_menu_id;
        /**
         * The ID of the menu item this target belongs to, only used when
         * calling host targets from the plugin.
         *
         * NOTE: Needed to work around a Bitwig bug, see the comment in
         *       `ExecuteMenuItem`
         */
        int32 item_id;
        /**
         * The tag of the menu item this target belongs to.
         */
        int32 tag;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value8b(context_menu_id);
            s.value4b(item_id);
            s.value4b(tag);
        }
    };

    /**
     * Create context menu target that when called, calls the corresponding
     * context menu target provided by the object.
     */
    YaContextMenuTarget(ConstructArgs&& args) noexcept;

    virtual ~YaContextMenuTarget() noexcept;

    DECLARE_FUNKNOWN_METHODS

    /**
     * Get the instance ID of the owner of this object.
     */
    inline size_t owner_instance_id() const noexcept {
        return arguments_.owner_instance_id;
    }

    /**
     * Get the unique ID for the context menu this target belongs to.
     */
    inline size_t context_menu_id() const { return arguments_.context_menu_id; }

    /**
     * Get the ID of the menu item this target was obtained from. This value is
     * only actually used when calling host context menu items from a plugin.
     */
    inline int32 item_id() const { return arguments_.item_id; }

    /**
     * Get the tag of the menu item this target was passed to.
     */
    inline int32 target_tag() const { return arguments_.tag; }

    /*
     * Message to pass through a call to
     * `IContextMenuTarget::executeMenuItem(tag)` to the proxied object provided
     * by the plugin.
     */
    struct ExecuteMenuItem {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;
        native_size_t context_menu_id;
        /**
         * The menu item ID this target belongs to.
         *
         * This is used when calling host context menu items from the plugin's
         * side.
         *
         * NOTE: This is needed because Bitwig identifies its own menu items by
         *       opaque ID, and not through the tag. They use 0 for all tags.
         */
        int32 item_id;
        /**
         * The tag of the target this method was called on. Presumably this
         * would always be the same as the `tag` argument passed to this
         * function, but it doesn't have to be.
         *
         * This is used when calling plugin context menu items from the host's
         * side.
         */
        int32 target_tag;

        int32 tag;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value8b(context_menu_id);
            s.value4b(item_id);
            s.value4b(target_tag);
            s.value4b(tag);
        }
    };

    virtual tresult PLUGIN_API executeMenuItem(int32 tag) override = 0;

   protected:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop
