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

#include <pluginterfaces/vst/ivsteditcontroller.h>
#include "bitsery/ext/std_optional.h"

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
        ConstructArgs();

        /**
         * Check whether an existing implementation implements
         * `IComponentHandlerBusActivation` and read arguments from it.
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
    YaComponentHandlerBusActivation(const ConstructArgs&& args);

    inline bool supported() const { return arguments.supported; }

    virtual tresult PLUGIN_API
    requestBusActivation(Steinberg::Vst::MediaType type,
                         Steinberg::Vst::BusDirection dir,
                         int32 index,
                         TBool state) override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop
