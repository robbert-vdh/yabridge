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

#include <pluginterfaces/vst/ivsteditcontroller.h>

#include "../../common.h"
#include "../base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IEditController{,2}` for serialization purposes. This is
 * instantiated as part of `Vst3PluginProxy`.
 *
 * Steinberg forgot to inherit `IEditController2` from `IEditController` event
 * if it says it does in the docs, so we'll pretend they just that.
 */
class YaEditController2 : public Steinberg::Vst::IEditController,
                          public Steinberg::Vst::IEditController2 {
   public:
    /**
     * These are the arguments for creating a `YaEditController2`.
     */
    struct ConstructArgs {
        ConstructArgs();

        /**
         * Check whether an existing implementation implements `IEditController`
         * and `IEditController2` and read arguments from it.
         */
        ConstructArgs(Steinberg::IPtr<Steinberg::FUnknown> object);

        /**
         * Whether the object supported `IEditController`.
         */
        bool supported_version_1;

        /**
         * Whether the object supported `IEditController2`.
         */
        bool supported_version_2;

        template <typename S>
        void serialize(S& s) {
            s.value1b(supported_version_1);
            s.value1b(supported_version_2);
        }
    };

    /**
     * Instantiate this instance with arguments read from another interface
     * implementation.
     */
    YaEditController2(const ConstructArgs&& args);

    inline bool supported_version_1() const {
        return arguments.supported_version_1;
    }
    inline bool supported_version_2() const {
        return arguments.supported_version_2;
    }

    // From `IEditController`

    /**
     * Message to pass through a call to
     * `IEditController::setComponentState(state)` to the Wine plugin host.
     */
    struct SetComponentState {
        using Response = UniversalTResult;

        native_size_t instance_id;

        VectorStream state;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.object(state);
        }
    };

    virtual tresult PLUGIN_API
    setComponentState(Steinberg::IBStream* state) override = 0;

    // `setState()` and `getState()` are defiend in both `IComponent` and
    // `IEditController`. Since an object can only ever implement one or the
    // other, the messages for calling either are defined directly on
    // `Vst3PluginProxy`.
    virtual tresult PLUGIN_API
    setState(Steinberg::IBStream* state) override = 0;
    virtual tresult PLUGIN_API
    getState(Steinberg::IBStream* state) override = 0;

    /**
     * Message to pass through a call to `IEditController::getParameterCount()`
     * to the Wine plugin host.
     */
    struct GetParameterCount {
        using Response = PrimitiveWrapper<int32>;

        native_size_t instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
        }
    };

    virtual int32 PLUGIN_API getParameterCount() override = 0;
    virtual tresult PLUGIN_API
    getParameterInfo(int32 paramIndex,
                     Steinberg::Vst::ParameterInfo& info /*out*/) override = 0;
    virtual tresult PLUGIN_API getParamStringByValue(
        Steinberg::Vst::ParamID id,
        Steinberg::Vst::ParamValue valueNormalized /*in*/,
        Steinberg::Vst::String128 string /*out*/) override = 0;
    virtual tresult PLUGIN_API getParamValueByString(
        Steinberg::Vst::ParamID id,
        Steinberg::Vst::TChar* string /*in*/,
        Steinberg::Vst::ParamValue& valueNormalized /*out*/) override = 0;
    virtual Steinberg::Vst::ParamValue PLUGIN_API normalizedParamToPlain(
        Steinberg::Vst::ParamID id,
        Steinberg::Vst::ParamValue valueNormalized) override = 0;
    virtual Steinberg::Vst::ParamValue PLUGIN_API
    plainParamToNormalized(Steinberg::Vst::ParamID id,
                           Steinberg::Vst::ParamValue plainValue) override = 0;
    virtual Steinberg::Vst::ParamValue PLUGIN_API
    getParamNormalized(Steinberg::Vst::ParamID id) override = 0;
    virtual tresult PLUGIN_API
    setParamNormalized(Steinberg::Vst::ParamID id,
                       Steinberg::Vst::ParamValue value) override = 0;
    virtual tresult PLUGIN_API setComponentHandler(
        Steinberg::Vst::IComponentHandler* handler) override = 0;
    virtual Steinberg::IPlugView* PLUGIN_API
    createView(Steinberg::FIDString name) override = 0;

    // From `IEditController2`

    virtual tresult PLUGIN_API
    setKnobMode(Steinberg::Vst::KnobMode mode) override = 0;
    virtual tresult PLUGIN_API openHelp(TBool onlyCheck) override = 0;
    virtual tresult PLUGIN_API openAboutBox(TBool onlyCheck) override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop
