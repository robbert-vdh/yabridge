// yabridge: a Wine VST bridge
// Copyright (C) 2020  Robbert van der Helm
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
#include "plug-view/plug-view.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * An abstract class that implements `IPlugView`, and optionally also all
 * extensions to `IPlugView` depending on what the plugin's implementation
 * supports. This provides a proxy for the `IPlugView*` returned by a plugin on
 * `IEditController::createView()`, and it works exactly the same as
 * `Vst3PluginProxy`.
 */
class Vst3PlugViewProxy : public YaPlugView {
   public:
    /**
     * These are the arguments for constructing a
     * `Vst3PlugViewProxyImpl`.
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
         * The unique instance identifier of the proxy object that returned this
         * `IPlugView*`. This way we can refer to the correct 'actual'
         * `IPlugView*` when the host calls a function on this object.
         */
        native_size_t owner_instance_id;

        YaPlugView::ConstructArgs plug_view_args;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.object(plug_view_args);
        }
    };

    /**
     * Instantiate this instance with arguments read from an actual component
     * handler.
     *
     * @note Since this is passed as part of `IEditController::createView()`,
     *   there are is no direct `Construct`
     *   message. The destructor should still send a message to drop the
     *   original smart pointer.
     */
    Vst3PlugViewProxy(const ConstructArgs&& args);

    /**
     * Message to request the Wine plugin host to destroy the `IPlugView*`
     * returned by the object with the given instance ID. Sent from the
     * destructor of `Vst3PlugViewProxyImpl`.
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
     * @remark The plugin side implementation should send a control message to
     *   clean up the instance on the Wine side in its destructor.
     */
    virtual ~Vst3PlugViewProxy() = 0;

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
