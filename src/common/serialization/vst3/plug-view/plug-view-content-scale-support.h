// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2023 Robbert van der Helm
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

#include <pluginterfaces/gui/iplugviewcontentscalesupport.h>

#include "../../common.h"
#include "../base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IPlugViewContentScaleSupport` for serialization purposes. This
 * is instantiated as part of `Vst3PlugViewProxy`.
 */
class YaPlugViewContentScaleSupport
    : public Steinberg::IPlugViewContentScaleSupport {
   public:
    /**
     * These are the arguments for creating a `YaPlugViewContentScaleSupport`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements
         * `IPlugViewContentScaleSupport` and read arguments from it.
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
    YaPlugViewContentScaleSupport(ConstructArgs&& args) noexcept;

    virtual ~YaPlugViewContentScaleSupport() noexcept = default;

    inline bool supported() const noexcept { return arguments_.supported; }

    /**
     * Message to pass through a call to
     * `IPlugViewContentScaleSupport::setContentScaleFactor(factor)` to the Wine
     * plugin host.
     */
    struct SetContentScaleFactor {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        ScaleFactor factor;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value4b(factor);
        }
    };

    virtual tresult PLUGIN_API
    setContentScaleFactor(ScaleFactor factor) override = 0;

   protected:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop
