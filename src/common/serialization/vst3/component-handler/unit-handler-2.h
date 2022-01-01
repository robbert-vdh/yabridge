// yabridge: a Wine VST bridge
// Copyright (C) 2020-2022 Robbert van der Helm
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

#include <pluginterfaces/vst/ivstunits.h>

#include "../../common.h"
#include "../base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IUnitHandler2` for serialization purposes. This is instantiated
 * as part of `Vst3ComponentHandlerProxy`.
 */
class YaUnitHandler2 : public Steinberg::Vst::IUnitHandler2 {
   public:
    /**
     * These are the arguments for creating a `YaUnitHandler2`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements `IUnitHandler2`
         * and read arguments from it.
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
    YaUnitHandler2(ConstructArgs&& args) noexcept;

    inline bool supported() const noexcept { return arguments_.supported; }

    /**
     * Message to pass through a call to
     * `IUnitHandler2::notifyUnitByBusChange()` to the unit handler provided by
     * the host.
     */
    struct NotifyUnitByBusChange {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
        }
    };

    virtual tresult PLUGIN_API notifyUnitByBusChange() override = 0;

   protected:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop
