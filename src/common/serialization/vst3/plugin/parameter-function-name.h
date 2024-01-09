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

#include <pluginterfaces/vst/ivstparameterfunctionname.h>

#include "../../common.h"
#include "../base.h"
#include "../host-context-proxy.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IParameterFunctionName` for serialization purposes. Both
 * components and edit controllers inherit from this. This is instantiated as
 * part of `Vst3PluginProxy`.
 */
class YaParameterFunctionName : public Steinberg::Vst::IParameterFunctionName {
   public:
    /**
     * These are the arguments for creating a `YaParameterFunctionName`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements
         * `IParameterFunctionName` and read arguments from it.
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
    YaParameterFunctionName(ConstructArgs&& args) noexcept;

    virtual ~YaParameterFunctionName() noexcept = default;

    inline bool supported() const noexcept { return arguments_.supported; }

    /**
     * The response code and returned parameter ID for a call to
     * `IParameterFunctionName::getParameterIDFromFunctionName(unit_id,
     * function_name, &param_id)`.
     */
    struct GetParameterIDFromFunctionNameResponse {
        UniversalTResult result;
        Steinberg::Vst::ParamID param_id;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.value4b(param_id);
        }
    };

    /**
     * Message to pass through a call to
     * `IParameterFunctionName::getParameterIDFromFunctionName(unit_id,
     * function_name, &param_id)` to the Wine plugin host.
     */
    struct GetParameterIDFromFunctionName {
        using Response = GetParameterIDFromFunctionNameResponse;

        native_size_t instance_id;

        Steinberg::Vst::UnitID unit_id;
        std::string function_name;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(unit_id);
            s.text1b(function_name, 1024);
        }
    };

    virtual tresult PLUGIN_API getParameterIDFromFunctionName(
        Steinberg::Vst::UnitID unitID,
        Steinberg::FIDString functionName,
        Steinberg::Vst::ParamID& paramID) override = 0;

   protected:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop
