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

#include <pluginterfaces/vst/ivsthostapplication.h>

#include "../vst3.h"

class Vst3PluginProxyImpl : public Vst3PluginProxy {
   public:
    Vst3PluginProxyImpl(Vst3PluginBridge& bridge,
                        Vst3PluginProxy::ConstructArgs&& args);

    /**
     * When the reference count reaches zero and this destructor is called,
     * we'll send a request to the Wine plugin host to destroy the corresponding
     * object.
     */
    ~Vst3PluginProxyImpl();

    /**
     * We'll override the query interface to log queries for interfaces we do
     * not (yet) support.
     */
    tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                      void** obj) override;

    inline size_t instance_id() { return arguments.instance_id; }

    // From `IAudioProcessor`
    tresult PLUGIN_API
    setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs,
                       int32 numIns,
                       Steinberg::Vst::SpeakerArrangement* outputs,
                       int32 numOuts) override;
    tresult PLUGIN_API
    getBusArrangement(Steinberg::Vst::BusDirection dir,
                      int32 index,
                      Steinberg::Vst::SpeakerArrangement& arr) override;
    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) override;
    uint32 PLUGIN_API getLatencySamples() override;
    tresult PLUGIN_API
    setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    tresult PLUGIN_API setProcessing(TBool state) override;
    tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    uint32 PLUGIN_API getTailSamples() override;

    // From `IComponent`
    tresult PLUGIN_API setIoMode(Steinberg::Vst::IoMode mode) override;
    int32 PLUGIN_API getBusCount(Steinberg::Vst::MediaType type,
                                 Steinberg::Vst::BusDirection dir) override;
    tresult PLUGIN_API
    getBusInfo(Steinberg::Vst::MediaType type,
               Steinberg::Vst::BusDirection dir,
               int32 index,
               Steinberg::Vst::BusInfo& bus /*out*/) override;
    tresult PLUGIN_API
    getRoutingInfo(Steinberg::Vst::RoutingInfo& inInfo,
                   Steinberg::Vst::RoutingInfo& outInfo /*out*/) override;
    tresult PLUGIN_API activateBus(Steinberg::Vst::MediaType type,
                                   Steinberg::Vst::BusDirection dir,
                                   int32 index,
                                   TBool state) override;
    tresult PLUGIN_API setActive(TBool state) override;
    tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    // From `IConnectionPoint`
    tresult PLUGIN_API connect(IConnectionPoint* other) override;
    tresult PLUGIN_API disconnect(IConnectionPoint* other) override;
    tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

    // From `IEditController`
    tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    // `IEditController` also contains `getState()` and `setState()`  functions.
    // These are identical to those defiend in `IComponent` and they're thus
    // handled in in the same function.
    int32 PLUGIN_API getParameterCount() override;
    tresult PLUGIN_API
    getParameterInfo(int32 paramIndex,
                     Steinberg::Vst::ParameterInfo& info /*out*/) override;
    tresult PLUGIN_API
    getParamStringByValue(Steinberg::Vst::ParamID id,
                          Steinberg::Vst::ParamValue valueNormalized /*in*/,
                          Steinberg::Vst::String128 string /*out*/) override;
    tresult PLUGIN_API getParamValueByString(
        Steinberg::Vst::ParamID id,
        Steinberg::Vst::TChar* string /*in*/,
        Steinberg::Vst::ParamValue& valueNormalized /*out*/) override;
    Steinberg::Vst::ParamValue PLUGIN_API
    normalizedParamToPlain(Steinberg::Vst::ParamID id,
                           Steinberg::Vst::ParamValue valueNormalized) override;
    Steinberg::Vst::ParamValue PLUGIN_API
    plainParamToNormalized(Steinberg::Vst::ParamID id,
                           Steinberg::Vst::ParamValue plainValue) override;
    Steinberg::Vst::ParamValue PLUGIN_API
    getParamNormalized(Steinberg::Vst::ParamID id) override;
    tresult PLUGIN_API
    setParamNormalized(Steinberg::Vst::ParamID id,
                       Steinberg::Vst::ParamValue value) override;
    tresult PLUGIN_API
    setComponentHandler(Steinberg::Vst::IComponentHandler* handler) override;
    Steinberg::IPlugView* PLUGIN_API
    createView(Steinberg::FIDString name) override;

    // From `IEditController2`
    tresult PLUGIN_API setKnobMode(Steinberg::Vst::KnobMode mode) override;
    tresult PLUGIN_API openHelp(TBool onlyCheck) override;
    tresult PLUGIN_API openAboutBox(TBool onlyCheck) override;

    // From `IPluginBase`
    tresult PLUGIN_API initialize(FUnknown* context) override;
    tresult PLUGIN_API terminate() override;

   private:
    Vst3PluginBridge& bridge;

    /**
     * An `IHostApplication` instance if we get one through
     * `IPluginBase::initialize()`. This should be the same for all plugin
     * instances so we should not have to store it here separately, but for the
     * sake of correctness we will.
     */
    Steinberg::FUnknownPtr<Steinberg::Vst::IHostApplication>
        host_application_context;
};
