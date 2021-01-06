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
     * Create context menu target that when called, calls the corresponding
     * context menu target provided by the object.
     *
     * @param owner_instance_id The object instance that this target's context
     *   menu belongs to.
     * @param context_menu_id The unique ID of the context menu requested by
     *   `owwner_instance_id`.
     * @param tag The tag of the menu item this target belongs to.
     */
    YaContextMenuTarget(native_size_t owner_instance_id,
                        native_size_t context_menu_id,
                        int32 tag);

    ~YaContextMenuTarget();

    DECLARE_FUNKNOWN_METHODS

    virtual tresult PLUGIN_API executeMenuItem(int32 tag) override = 0;

   protected:
    native_size_t owner_instance_id;
    native_size_t context_menu_id;
    int32 tag;
};

#pragma GCC diagnostic pop
