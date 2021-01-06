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
 * TODO: None of this has been tested because no host on Linux implement this.
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
                      size_t owner_instance_id,
                      size_t context_menu_id);

        /**
         * The unique instance identifier of the proxy object instance this
         * context menu has been created for.
         */
        native_size_t owner_instance_id;
        /**
         * A unique identifier for this specific context menu. Having more than
         * one context menu at a time will be impossible, but in case the plugin
         * for whatever reason hangs on to the pointer of an old context menu
         * after it has opened a new one, we would not want the new context menu
         * to get destroyed when it drops the old pointer.
         */
        native_size_t context_menu_id;

        YaContextMenu::ConstructArgs context_menu_args;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value8b(context_menu_id);
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
     *
     * @note The lifecycle of these objects should be tracked in an
     *   `std::map<size_t, Vst3ContextMenuProxy*>` in the `InstanceInterfaces`
     *   struct. We need to use raw pointers or references here so we can refer
     *   to the object without interfering with the reference count.
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
        native_size_t context_menu_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value8b(context_menu_id);
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

    /**
     * Get the unique ID for this context menu.
     */
    inline size_t context_menu_id() const { return arguments.context_menu_id; }

   private:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop
