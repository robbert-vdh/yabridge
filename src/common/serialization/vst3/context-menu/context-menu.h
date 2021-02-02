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
#include "bitsery/ext/std_optional.h"

#include "../../common.h"
#include "../base.h"
#include "../context-menu-target.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IContextMenu` for serialization purposes. This is instantiated
 * as part of `Vst3ContextMenuProxy`.
 */
class YaContextMenu : public Steinberg::Vst::IContextMenu {
   public:
    /**
     * These are the arguments for creating a `YaContextMenu`.
     */
    struct ConstructArgs {
        ConstructArgs();

        /**
         * Check whether an existing implementation implements `IContextMenu`
         * and read arguments from it.
         */
        ConstructArgs(Steinberg::IPtr<Steinberg::FUnknown> object);

        /**
         * Whether the object supported this interface.
         */
        bool supported;

        template <typename S>
        void serialize(S& s) {
            s.value1b(supported);
        }
    };

    /**
     * Instantiate this instance with arguments read from another interface
     * implementation.
     */
    YaContextMenu(const ConstructArgs&& args);

    inline bool supported() const { return arguments.supported; }

    /**
     * Message to pass through a call to `IContextMenu::getItemCount()` to the
     * corresponding context menu instance returned by the host.
     */
    struct GetItemCount {
        using Response = PrimitiveWrapper<int32>;

        native_size_t owner_instance_id;
        native_size_t context_menu_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value8b(context_menu_id);
        }
    };

    virtual int32 PLUGIN_API getItemCount() override = 0;

    // XXX: Can a plugin call this to get items created by the host? Why would
    //      they do that? We should find a host/plugin combination that supports
    //      `IComponentHandler3` first.
    virtual tresult PLUGIN_API
    getItem(int32 index,
            Steinberg::Vst::IContextMenuItem& item /*out*/,
            Steinberg::Vst::IContextMenuTarget** target /*out*/) override = 0;

    /**
     * Message to pass through a call to `IContextMenu::addItem(item, <target>)`
     * to the corresponding context menu instance returned by the host. We'll
     * create a proxy for `target` based on `item->tag` on the plugin side that
     * forwards a call to the original target passed by the Windows VST3 plugin.
     */
    struct AddItem {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;
        native_size_t context_menu_id;

        // Steinberg seems to hav emessed up their naming scheme here, since
        // this is most definitely not an interface
        Steinberg::Vst::IContextMenuItem item;

        /**
         * Will be a nullopt if the plugin does not pass a `target` pointer. I'm
         * not sure if this is optional since there are no implementations for
         * this interface to be found, but I can imagine that this could be
         * optional for disabled menu items or for group starts/ends.
         */
        std::optional<YaContextMenuTarget::ConstructArgs> target;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value8b(context_menu_id);
            s.object(item);
            s.ext(target, bitsery::ext::StdOptional{});
        }
    };

    virtual tresult PLUGIN_API
    addItem(const Steinberg::Vst::IContextMenuItem& item,
            Steinberg::Vst::IContextMenuTarget* target) override = 0;

    /**
     * Message to pass through a call to `IContextMenu::removeItem(item,
     * <target>)` to the corresponding context menu instance returned by the
     * host. We'll pass the target already stored in our `Vst3PluginProxyImpl`
     * object. Not sure why it is even needed here.
     */
    struct RemoveItem {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;
        native_size_t context_menu_id;

        Steinberg::Vst::IContextMenuItem item;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value8b(context_menu_id);
            s.object(item);
        }
    };

    virtual tresult PLUGIN_API
    removeItem(const Steinberg::Vst::IContextMenuItem& item,
               Steinberg::Vst::IContextMenuTarget* target) override = 0;

    /**
     * Message to pass through a call to `IContextMenu::popup(x, y)` to the
     * corresponding context menu instance returned by the host.
     */
    struct Popup {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;
        native_size_t context_menu_id;

        Steinberg::UCoord x;
        Steinberg::UCoord y;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value8b(context_menu_id);
            s.value4b(x);
            s.value4b(y);
        }
    };

    virtual tresult PLUGIN_API popup(Steinberg::UCoord x,
                                     Steinberg::UCoord y) override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop

namespace Steinberg {
namespace Vst {
template <typename S>
void serialize(S& s, IContextMenuItem& item) {
    s.container2b(item.name);
    s.value4b(item.tag);
    s.value4b(item.flags);
}
}  // namespace Vst
}  // namespace Steinberg
