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

#include "../common.h"
#include "plug-frame/plug-frame.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * An abstract class that implements `IPlugFrame`, and optionally also all other
 * VST3 interfaces an object passed to `IPlugView::setFrame()` might implement.
 * This works exactly the same as `Vst3PluginProxy`, but instead of proxying for
 * an object provided by the plugin we are proxying for the `IPlugFrame*`
 * argument passed to plugin by the host.
 */
class Vst3PlugFrameProxy : public YaPlugFrame {
   public:
    /**
     * These are the arguments for constructing a
     * `Vst3PlugFrameProxyImpl`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Read from an existing object. We will try to mimic this object, so
         * we'll support any interfaces this object also supports.
         */
        ConstructArgs(Steinberg::IPtr<FUnknown> object,
                      size_t owner_instance_id) noexcept;

        /**
         * The unique instance identifier of the proxy object instance this
         * component handler has been passed to and thus belongs to. This way we
         * can refer to the correct 'actual' `IPlugFrame` instance when the
         * plugin does a callback.
         */
        native_size_t owner_instance_id;

        YaPlugFrame::ConstructArgs plug_frame_args;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.object(plug_frame_args);
        }
    };

    /**
     * Instantiate this instance with arguments read from an actual component
     * handler.
     *
     * @note Since this is passed as part of `IEditController::setPlugFrame()`,
     *   there are no direct `Construct` or `Destruct` messages. This object's
     *   lifetime is bound to that of the objects they are passed to. If the
     *   plug view instance gets dropped, this proxy should also be dropped.
     */
    Vst3PlugFrameProxy(ConstructArgs&& args) noexcept;

    /**
     * The lifetime of this object should be bound to the object we created it
     * for. When the `Vst3PlugViewProxy` for the object with instance with id
     * `n` gets dropped, the corresponding `Vst3PlugFrameProxy` should also be
     * dropped.
     */
    virtual ~Vst3PlugFrameProxy() noexcept;

    DECLARE_FUNKNOWN_METHODS

    /**
     * Get the instance ID of the owner of this object.
     */
    inline size_t owner_instance_id() const noexcept {
        return arguments_.owner_instance_id;
    }

   private:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop
