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

#include "../../../bitsery/ext/in-place-optional.h"
#include "../../common.h"
#include "../base.h"
#include "../context-menu-proxy.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IComponentHandler3` for serialization purposes. This is
 * instantiated as part of `Vst3ComponentHandler3Proxy`.
 */
class YaComponentHandler3 : public Steinberg::Vst::IComponentHandler3 {
   public:
    /**
     * These are the arguments for creating a `YaComponentHandler3`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements
         * `IComponentHandler3` and read arguments from it.
         */
        ConstructArgs(Steinberg::IPtr<Steinberg::FUnknown> object) noexcept;

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
    YaComponentHandler3(ConstructArgs&& args) noexcept;

    virtual ~YaComponentHandler3() noexcept = default;

    inline bool supported() const noexcept { return arguments_.supported; }

    /**
     * The arguments needed to create a proxy object for the context menu
     * returned by the host resulting from a call to
     * `IComponentHandler3::createContextMenu(<plug_view>, param_id)`, if the
     * call succeeded.
     */
    struct CreateContextMenuResponse {
        std::optional<Vst3ContextMenuProxy::ConstructArgs> context_menu_args;

        template <typename S>
        void serialize(S& s) {
            s.ext(context_menu_args, bitsery::ext::InPlaceOptional{});
        }
    };

    /**
     * Message to pass through a call to
     * `IComponentHandler3::createContextMenu(<plug_view>, param_id)` to the
     * component handler provided by the host.
     *
     * XXX: Since we don't support multiple `IPlugView`s right now (as it's not
     *      used the SDK's current version), we'll just assume that `view` is
     *      the view stored in `Vst3PluginProxyImpl::plug_view`
     */
    struct CreateContextMenu {
        using Response = CreateContextMenuResponse;

        native_size_t owner_instance_id;

        // XXX: Why do they pass a pointer to the parameter ID? The docs that
        //      when the parameter ID is zero, the host should create a generic
        //      context menu. Did they mean to write 'a null pointer' here?
        std::optional<Steinberg::Vst::ParamID> param_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.ext(param_id, bitsery::ext::InPlaceOptional{},
                  [](S& s, Steinberg::Vst::ParamID& id) { s.value4b(id); });
        }
    };

    virtual Steinberg::Vst::IContextMenu* PLUGIN_API
    createContextMenu(Steinberg::IPlugView* plugView,
                      const Steinberg::Vst::ParamID* paramID) override = 0;

   protected:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop
