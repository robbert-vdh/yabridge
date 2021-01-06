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

#include <pluginterfaces/gui/iplugview.h>

#include "../../common.h"
#include "../base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IPlugFrame` for serialization purposes. This is instantiated as
 * part of `Vst3PlugFrameProxy`.
 */
class YaPlugFrame : public Steinberg::IPlugFrame {
   public:
    /**
     * These are the arguments for creating a `YaPlugFrame`.
     */
    struct ConstructArgs {
        ConstructArgs();

        /**
         * Check whether an existing implementation implements `IPlugFrame` and
         * read arguments from it.
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
    YaPlugFrame(const ConstructArgs&& args);

    inline bool supported() const { return arguments.supported; }

    /**
     * Message to pass through a call to `IPlugFrame::resizeView(<plug_view>,
     * new_size)` to the `IPlugView` object provided by the host.
     *
     * XXX: Since we don't support multiple `IPlugView`s right now (as it's not
     *      used the SDK's current version), we'll just assume that `view` is
     *      the view stored in `Vst3PluginProxyImpl::plug_view`
     */
    struct ResizeView {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        Steinberg::ViewRect new_size;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.object(new_size);
        }
    };

    virtual tresult PLUGIN_API
    resizeView(Steinberg::IPlugView* view,
               Steinberg::ViewRect* newSize) override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop
