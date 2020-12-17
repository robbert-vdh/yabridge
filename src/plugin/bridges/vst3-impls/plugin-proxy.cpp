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

#include "plugin-proxy.h"

Vst3PluginProxyImpl::Vst3PluginProxyImpl(Vst3PluginBridge& bridge,
                                         Vst3PluginProxy::ConstructArgs&& args)
    : Vst3PluginProxy(std::move(args)), bridge(bridge) {
    bridge.register_plugin_proxy(*this);
}

Vst3PluginProxyImpl::~Vst3PluginProxyImpl() {
    bridge.send_message(
        Vst3PluginProxy::Destruct{.instance_id = arguments.instance_id});
    bridge.unregister_plugin_proxy(arguments.instance_id);
}

tresult PLUGIN_API
Vst3PluginProxyImpl::queryInterface(const Steinberg::TUID _iid, void** obj) {
    // TODO: Successful queries should also be logged
    const tresult result = Vst3PluginProxy::queryInterface(_iid, obj);
    if (result != Steinberg::kResultOk) {
        bridge.logger.log_unknown_interface("In IComponent::queryInterface()",
                                            Steinberg::FUID::fromTUID(_iid));
    }

    return result;
}

tresult PLUGIN_API Vst3PluginProxyImpl::setBusArrangements(
    Steinberg::Vst::SpeakerArrangement* inputs,
    int32 numIns,
    Steinberg::Vst::SpeakerArrangement* outputs,
    int32 numOuts) {
    assert(inputs && outputs);
    return bridge.send_message(YaAudioProcessor::SetBusArrangements{
        .instance_id = arguments.instance_id,
        .inputs = std::vector<Steinberg::Vst::SpeakerArrangement>(
            inputs, &inputs[numIns]),
        .num_ins = numIns,
        .outputs = std::vector<Steinberg::Vst::SpeakerArrangement>(
            outputs, &outputs[numOuts]),
        .num_outs = numOuts,
    });
}

tresult PLUGIN_API Vst3PluginProxyImpl::getBusArrangement(
    Steinberg::Vst::BusDirection dir,
    int32 index,
    Steinberg::Vst::SpeakerArrangement& arr) {
    const GetBusArrangementResponse response =
        bridge.send_message(YaAudioProcessor::GetBusArrangement{
            .instance_id = arguments.instance_id,
            .dir = dir,
            .index = index,
            .arr = arr});

    arr = response.updated_arr;

    return response.result;
}

tresult PLUGIN_API
Vst3PluginProxyImpl::canProcessSampleSize(int32 symbolicSampleSize) {
    return bridge.send_message(YaAudioProcessor::CanProcessSampleSize{
        .instance_id = arguments.instance_id,
        .symbolic_sample_size = symbolicSampleSize});
}

uint32 PLUGIN_API Vst3PluginProxyImpl::getLatencySamples() {
    return bridge.send_message(YaAudioProcessor::GetLatencySamples{
        .instance_id = arguments.instance_id});
}

tresult PLUGIN_API
Vst3PluginProxyImpl::setupProcessing(Steinberg::Vst::ProcessSetup& setup) {
    return bridge.send_message(YaAudioProcessor::SetupProcessing{
        .instance_id = arguments.instance_id, .setup = setup});
}

tresult PLUGIN_API Vst3PluginProxyImpl::setProcessing(TBool state) {
    return bridge.send_message(YaAudioProcessor::SetProcessing{
        .instance_id = arguments.instance_id, .state = state});
}

tresult PLUGIN_API
Vst3PluginProxyImpl::process(Steinberg::Vst::ProcessData& data) {
    ProcessResponse response = bridge.send_message(YaAudioProcessor::Process{
        .instance_id = arguments.instance_id, .data = data});

    response.output_data.write_back_outputs(data);

    return response.result;
}

uint32 PLUGIN_API Vst3PluginProxyImpl::getTailSamples() {
    return bridge.send_message(
        YaAudioProcessor::GetTailSamples{.instance_id = arguments.instance_id});
}

tresult PLUGIN_API Vst3PluginProxyImpl::setIoMode(Steinberg::Vst::IoMode mode) {
    return bridge.send_message(YaComponent::SetIoMode{
        .instance_id = arguments.instance_id, .mode = mode});
}

int32 PLUGIN_API
Vst3PluginProxyImpl::getBusCount(Steinberg::Vst::MediaType type,
                                 Steinberg::Vst::BusDirection dir) {
    return bridge.send_message(YaComponent::GetBusCount{
        .instance_id = arguments.instance_id, .type = type, .dir = dir});
}

tresult PLUGIN_API
Vst3PluginProxyImpl::getBusInfo(Steinberg::Vst::MediaType type,
                                Steinberg::Vst::BusDirection dir,
                                int32 index,
                                Steinberg::Vst::BusInfo& bus /*out*/) {
    const GetBusInfoResponse response = bridge.send_message(
        YaComponent::GetBusInfo{.instance_id = arguments.instance_id,
                                .type = type,
                                .dir = dir,
                                .index = index,
                                .bus = bus});

    bus = response.updated_bus;
    return response.result;
}

tresult PLUGIN_API Vst3PluginProxyImpl::getRoutingInfo(
    Steinberg::Vst::RoutingInfo& inInfo,
    Steinberg::Vst::RoutingInfo& outInfo /*out*/) {
    const GetRoutingInfoResponse response = bridge.send_message(
        YaComponent::GetRoutingInfo{.instance_id = arguments.instance_id,
                                    .in_info = inInfo,
                                    .out_info = outInfo});

    inInfo = response.updated_in_info;
    outInfo = response.updated_out_info;
    return response.result;
}

tresult PLUGIN_API
Vst3PluginProxyImpl::activateBus(Steinberg::Vst::MediaType type,
                                 Steinberg::Vst::BusDirection dir,
                                 int32 index,
                                 TBool state) {
    return bridge.send_message(
        YaComponent::ActivateBus{.instance_id = arguments.instance_id,
                                 .type = type,
                                 .dir = dir,
                                 .index = index,
                                 .state = state});
}

tresult PLUGIN_API Vst3PluginProxyImpl::setActive(TBool state) {
    return bridge.send_message(YaComponent::SetActive{
        .instance_id = arguments.instance_id, .state = state});
}

tresult PLUGIN_API Vst3PluginProxyImpl::setState(Steinberg::IBStream* state) {
    // Since both interfaces contain this function, this is used for both
    // `IComponent::setState()` as well as `IEditController::setState()`
    return bridge.send_message(Vst3PluginProxy::SetState{
        .instance_id = arguments.instance_id, .state = state});
}

tresult PLUGIN_API Vst3PluginProxyImpl::getState(Steinberg::IBStream* state) {
    // Since both interfaces contain this function, this is used for both
    // `IComponent::getState()` as well as `IEditController::getState()`
    const GetStateResponse response = bridge.send_message(
        Vst3PluginProxy::GetState{.instance_id = arguments.instance_id});

    assert(response.updated_state.write_back(state) == Steinberg::kResultOk);

    return response.result;
}

tresult PLUGIN_API
Vst3PluginProxyImpl::setComponentState(Steinberg::IBStream* state) {
    return bridge.send_message(YaEditController2::SetComponentState{
        .instance_id = arguments.instance_id, .state = state});
}

int32 PLUGIN_API Vst3PluginProxyImpl::getParameterCount() {
    return bridge.send_message(YaEditController2::GetParameterCount{
        .instance_id = arguments.instance_id});
}

tresult PLUGIN_API Vst3PluginProxyImpl::getParameterInfo(
    int32 paramIndex,
    Steinberg::Vst::ParameterInfo& info /*out*/) {
    const GetParameterInfoResponse response =
        bridge.send_message(YaEditController2::GetParameterInfo{
            .instance_id = arguments.instance_id,
            .param_index = paramIndex,
            .info = info});

    info = response.updated_info;

    return response.result;
}

tresult PLUGIN_API Vst3PluginProxyImpl::getParamStringByValue(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue valueNormalized /*in*/,
    Steinberg::Vst::String128 string /*out*/) {
    // TODO: Implement
    bridge.logger.log("TODO IEditController::getParamStringByValue()");
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API Vst3PluginProxyImpl::getParamValueByString(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::TChar* string /*in*/,
    Steinberg::Vst::ParamValue& valueNormalized /*out*/) {
    // TODO: Implement
    bridge.logger.log("TODO IEditController::getParamValueByString()");
    return Steinberg::kNotImplemented;
}

Steinberg::Vst::ParamValue PLUGIN_API
Vst3PluginProxyImpl::normalizedParamToPlain(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue valueNormalized) {
    // TODO: Implement
    bridge.logger.log("TODO IEditController::normalizedParamToPlain()");
    return Steinberg::kNotImplemented;
}

Steinberg::Vst::ParamValue PLUGIN_API
Vst3PluginProxyImpl::plainParamToNormalized(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue plainValue) {
    // TODO: Implement
    bridge.logger.log("TODO IEditController::plainParamToNormalized()");
    return Steinberg::kNotImplemented;
}

Steinberg::Vst::ParamValue PLUGIN_API
Vst3PluginProxyImpl::getParamNormalized(Steinberg::Vst::ParamID id) {
    // TODO: Implement
    bridge.logger.log("TODO IEditController::getParamNormalized()");
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API
Vst3PluginProxyImpl::setParamNormalized(Steinberg::Vst::ParamID id,
                                        Steinberg::Vst::ParamValue value) {
    // TODO: Implement
    bridge.logger.log("TODO IEditController::setParamNormalized()");
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API Vst3PluginProxyImpl::setComponentHandler(
    Steinberg::Vst::IComponentHandler* handler) {
    // TODO: Implement
    bridge.logger.log("TODO IEditController::setComponentHandler()");
    return Steinberg::kNotImplemented;
}

Steinberg::IPlugView* PLUGIN_API
Vst3PluginProxyImpl::createView(Steinberg::FIDString name) {
    // TODO: Implement
    bridge.logger.log("TODO IEditController::createView()");
    return nullptr;
}

tresult PLUGIN_API
Vst3PluginProxyImpl::setKnobMode(Steinberg::Vst::KnobMode mode) {
    // TODO: Implement
    bridge.logger.log("TODO IEditController2::setKnobMode()");
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API Vst3PluginProxyImpl::openHelp(TBool onlyCheck) {
    // TODO: Implement
    bridge.logger.log("TODO IEditController2::openHelp()");
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API Vst3PluginProxyImpl::openAboutBox(TBool onlyCheck) {
    // TODO: Implement
    bridge.logger.log("TODO IEditController2::openAboutBox()");
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API Vst3PluginProxyImpl::initialize(FUnknown* context) {
    // This `context` will likely be an `IHostApplication`. If it is, we will
    // store it here, and we'll proxy through all calls to it made from the Wine
    // side. Otherwise we'll still call `IPluginBase::initialize()` but with a
    // null pointer instead.
    host_application_context = context;

    std::optional<YaHostApplication::ConstructArgs>
        host_application_context_args = std::nullopt;
    if (host_application_context) {
        host_application_context_args = YaHostApplication::ConstructArgs(
            host_application_context, arguments.instance_id);
    } else {
        bridge.logger.log_unknown_interface(
            "In IPluginBase::initialize()",
            context ? std::optional(context->iid) : std::nullopt);
    }

    return bridge.send_message(
        YaPluginBase::Initialize{.instance_id = arguments.instance_id,
                                 .host_application_context_args =
                                     std::move(host_application_context_args)});
}

tresult PLUGIN_API Vst3PluginProxyImpl::terminate() {
    return bridge.send_message(
        YaPluginBase::Terminate{.instance_id = arguments.instance_id});
}
