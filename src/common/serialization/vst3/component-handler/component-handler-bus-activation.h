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

#include <pluginterfaces/vst/ivsteditcontroller.h>

#include "../../../bitsery/ext/in-place-optional.h"
#include "../../common.h"
#include "../base.h"
#include "../context-menu-proxy.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IComponentHandlerBusActivation` for serialization purposes.
 * This is instantiated as part of `Vst3ComponentHandlerBusActivationProxy`.
 */
class YaComponentHandlerBusActivation
    : public Steinberg::Vst::IComponentHandlerBusActivation {
   public:
    /**
     * These are the arguments for creating a `YaComponentHandlerBusActivation`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements
         * `IComponentHandlerBusActivation` and read arguments from it.
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
    YaComponentHandlerBusActivation(ConstructArgs&& args) noexcept;

    virtual ~YaComponentHandlerBusActivation() noexcept = default;

    inline bool supported() const noexcept { return arguments_.supported; }

    /**
     * Message to pass through a call to
     * `IComponentHandlerBusActivation::requestBusActivation(type, dir, index,
     * state)` to the component handler provided by the host.
     */
    struct RequestBusActivation {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        Steinberg::Vst::MediaType type;
        Steinberg::Vst::BusDirection dir;
        int32 index;
        TBool state;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value4b(type);
            s.value4b(dir);
            s.value4b(index);
            s.value1b(state);
        }
    };

    virtual tresult PLUGIN_API
    requestBusActivation(Steinberg::Vst::MediaType type,
                         Steinberg::Vst::BusDirection dir,
                         int32 index,
                         TBool state) override = 0;

   protected:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop
