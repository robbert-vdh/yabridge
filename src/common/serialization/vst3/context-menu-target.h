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
        ConstructArgs();

        /**
         * Read from an existing object. We will try to mimic this object, so
         * we'll support any interfaces this object also supports.
         *
         * @param owner_instance_id The object instance that this target's
         * context menu belongs to.
         * @param context_menu_id The unique ID of the context menu requested by
         *   `owwner_instance_id`.
         */
        ConstructArgs(native_size_t owner_instance_id,
                      native_size_t context_menu_id);

        native_size_t owner_instance_id;
        native_size_t context_menu_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value8b(context_menu_id);
        }
    };

    /**
     * Create context menu target that when called, calls the corresponding
     * context menu target provided by the object.
     */
    YaContextMenuTarget(const ConstructArgs&& args);

    ~YaContextMenuTarget();

    DECLARE_FUNKNOWN_METHODS

    /*
     * Message to pass through a call to
     * `IContextMenuTarget::executeMenuItem(tag)` to the proxied object provided
     * by the plugin.
     */
    struct ExecuteMenuItem {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;
        native_size_t context_menu_id;

        int32 tag;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value8b(context_menu_id);
            s.value4b(tag);
        }
    };

    virtual tresult PLUGIN_API executeMenuItem(int32 tag) override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop
