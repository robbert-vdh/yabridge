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

#include "../common.h"
#include "context-menu/context-menu.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * An abstract class that implements `IContextMenu`, and optionally also all
 * other VST3 interfaces an object returned by
 * `IComponentHandler3::createContextMenu()` might implement. This is used to
 * provide a proxy for the context menu object created by the host. The host
 * will return a (prepopulated, although that's invisible to the plugin) context
 * menu for right clicking on a specific parameter. The plugin can then add
 * their own items to it, and then have it appear at the specified coordinates.
 * Those items passed by the plugin contain callbacks that will be called when
 * the user clicks on them. As far as I'm aware, not a single Linux VST3 host
 * implements `IComponentHandler3` and thus provides support for these context
 * menus.
 *
 * NOTE: For simplicity's sake (and because this is going to be true 100% of the
 *       time) we'll assume a plugin can only have a single context menu open at
 *       a time. If a host does allow creating multiple context menus for
 *       different parameters at the same time, then we'll just have to add a
 *       unique instance ID to the context menu.
 */
class Vst3ContextMenuProxy : public YaContextMenu {
   public:
    /**
     * These are the arguments for constructing a `Vst3ContextMenuProxyImpl`.
     */
    struct ConstructArgs {
        ConstructArgs();

        /**
         * Read from an existing object. We will try to mimic this object, so
         * we'll support any interfaces this object also supports.
         */
        ConstructArgs(Steinberg::IPtr<FUnknown> object,
                      size_t owner_instance_id);

        /**
         * The unique instance identifier of the proxy object instance this
         * context menu has been created for.
         */
        native_size_t owner_instance_id;

        YaContextMenu::ConstructArgs context_menu_args;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.object(context_menu_args);
        }
    };

    /**
     * Instantiate this instance with arguments read from an actual component
     * handler.
     *
     * This object is created as part of
     * `IComponentHandler3::createContextMenu`, so there's no direct `Construct`
     * message. When the object's reference count reaches zero, we should
     * destroy the actual context menu object provided by the host using the
     * `Destruct` message.
     */
    Vst3ContextMenuProxy(const ConstructArgs&& args);

    /**
     * Message to request the plugin to drop the the `IContextMenu*` returned by
     * the host for the plugin instance with the given instance ID. Sent from
     * the destructor of `Vst3ContextMenuProxyImpl`.
     */
    struct Destruct {
        using Response = Ack;

        native_size_t owner_instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
        }
    };

    /**
     * When this object gets dropped, we sent a `Destruct` message to also drop
     * the pointer to the actual `IContextMenu*` returend by the host during
     * `IComponentHandler3::createContextMenu`.
     */
    virtual ~Vst3ContextMenuProxy() = 0;

    DECLARE_FUNKNOWN_METHODS

    /**
     * Get the instance ID of the owner of this object.
     */
    inline size_t owner_instance_id() const {
        return arguments.owner_instance_id;
    }

   private:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop
