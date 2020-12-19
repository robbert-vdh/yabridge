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
        Vst3PluginProxy::Destruct{.instance_id = instance_id()});
    bridge.unregister_plugin_proxy(instance_id());
}

tresult PLUGIN_API
Vst3PluginProxyImpl::queryInterface(const Steinberg::TUID _iid, void** obj) {
    // TODO: Successful queries should also be logged
    const tresult result = Vst3PluginProxy::queryInterface(_iid, obj);
    if (result != Steinberg::kResultOk) {
        bridge.logger.log_unknown_interface("In FUnknown::queryInterface()",
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
        .instance_id = instance_id(),
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
    const GetBusArrangementResponse response = bridge.send_message(
        YaAudioProcessor::GetBusArrangement{.instance_id = instance_id(),
                                            .dir = dir,
                                            .index = index,
                                            .arr = arr});

    arr = response.updated_arr;

    return response.result;
}

tresult PLUGIN_API
Vst3PluginProxyImpl::canProcessSampleSize(int32 symbolicSampleSize) {
    return bridge.send_message(YaAudioProcessor::CanProcessSampleSize{
        .instance_id = instance_id(),
        .symbolic_sample_size = symbolicSampleSize});
}

uint32 PLUGIN_API Vst3PluginProxyImpl::getLatencySamples() {
    return bridge.send_message(
        YaAudioProcessor::GetLatencySamples{.instance_id = instance_id()});
}

tresult PLUGIN_API
Vst3PluginProxyImpl::setupProcessing(Steinberg::Vst::ProcessSetup& setup) {
    return bridge.send_message(YaAudioProcessor::SetupProcessing{
        .instance_id = instance_id(), .setup = setup});
}

tresult PLUGIN_API Vst3PluginProxyImpl::setProcessing(TBool state) {
    return bridge.send_message(YaAudioProcessor::SetProcessing{
        .instance_id = instance_id(), .state = state});
}

tresult PLUGIN_API
Vst3PluginProxyImpl::process(Steinberg::Vst::ProcessData& data) {
    // TODO: Check whether reusing a `YaProcessData` object make a difference in
    //       terms of performance
    ProcessResponse response = bridge.send_message(
        YaAudioProcessor::Process{.instance_id = instance_id(), .data = data});

    response.output_data.write_back_outputs(data);

    return response.result;
}

uint32 PLUGIN_API Vst3PluginProxyImpl::getTailSamples() {
    return bridge.send_message(
        YaAudioProcessor::GetTailSamples{.instance_id = instance_id()});
}

tresult PLUGIN_API Vst3PluginProxyImpl::setIoMode(Steinberg::Vst::IoMode mode) {
    return bridge.send_message(
        YaComponent::SetIoMode{.instance_id = instance_id(), .mode = mode});
}

int32 PLUGIN_API
Vst3PluginProxyImpl::getBusCount(Steinberg::Vst::MediaType type,
                                 Steinberg::Vst::BusDirection dir) {
    return bridge.send_message(YaComponent::GetBusCount{
        .instance_id = instance_id(), .type = type, .dir = dir});
}

tresult PLUGIN_API
Vst3PluginProxyImpl::getBusInfo(Steinberg::Vst::MediaType type,
                                Steinberg::Vst::BusDirection dir,
                                int32 index,
                                Steinberg::Vst::BusInfo& bus /*out*/) {
    const GetBusInfoResponse response = bridge.send_message(
        YaComponent::GetBusInfo{.instance_id = instance_id(),
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
        YaComponent::GetRoutingInfo{.instance_id = instance_id(),
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
        YaComponent::ActivateBus{.instance_id = instance_id(),
                                 .type = type,
                                 .dir = dir,
                                 .index = index,
                                 .state = state});
}

tresult PLUGIN_API Vst3PluginProxyImpl::setActive(TBool state) {
    return bridge.send_message(
        YaComponent::SetActive{.instance_id = instance_id(), .state = state});
}

tresult PLUGIN_API Vst3PluginProxyImpl::setState(Steinberg::IBStream* state) {
    // Since both interfaces contain this function, this is used for both
    // `IComponent::setState()` as well as `IEditController::setState()`
    return bridge.send_message(Vst3PluginProxy::SetState{
        .instance_id = instance_id(), .state = state});
}

tresult PLUGIN_API Vst3PluginProxyImpl::getState(Steinberg::IBStream* state) {
    // Since both interfaces contain this function, this is used for both
    // `IComponent::getState()` as well as `IEditController::getState()`
    const GetStateResponse response = bridge.send_message(
        Vst3PluginProxy::GetState{.instance_id = instance_id()});

    assert(response.updated_state.write_back(state) == Steinberg::kResultOk);

    return response.result;
}

tresult PLUGIN_API Vst3PluginProxyImpl::connect(IConnectionPoint* other) {
    // When the host is trying to connect two plugin proxy objects, we can just
    // identify the other object by its instance IDs and then connect the
    // objects in the Wine plugin host directly
    if (auto other_proxy = dynamic_cast<Vst3PluginProxy*>(other)) {
        return bridge.send_message(YaConnectionPoint::Connect{
            .instance_id = instance_id(),
            .other_instance_id = other_proxy->instance_id()});
    } else {
        // TODO: Add support for `ConnectionProxy` and similar objects
        bridge.logger.log(
            "WARNING: The host passed a proxy proxy object to "
            "'IConnectionPoint::connect()'. This is currently not supported.");
        return Steinberg::kNotImplemented;
    }
}

tresult PLUGIN_API Vst3PluginProxyImpl::disconnect(IConnectionPoint* other) {
    // See `Vst3PluginProxyImpl::connect()`
    if (auto other_proxy = dynamic_cast<Vst3PluginProxy*>(other)) {
        return bridge.send_message(YaConnectionPoint::Disconnect{
            .instance_id = instance_id(),
            .other_instance_id = other_proxy->instance_id()});
    } else {
        // TODO: Add support for `ConnectionProxy` and similar objects
        bridge.logger.log(
            "WARNING: The host passed a proxy proxy object to "
            "'IConnectionPoint::disconnect()'. This is currently not "
            "supported.");
        return Steinberg::kNotImplemented;
    }
}

tresult PLUGIN_API
Vst3PluginProxyImpl::notify(Steinberg::Vst::IMessage* message) {
    // TODO: Implement
    bridge.logger.log("TODO IConnectionPoint::notify()");
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API
Vst3PluginProxyImpl::setComponentState(Steinberg::IBStream* state) {
    return bridge.send_message(YaEditController::SetComponentState{
        .instance_id = instance_id(), .state = state});
}

int32 PLUGIN_API Vst3PluginProxyImpl::getParameterCount() {
    return bridge.send_message(
        YaEditController::GetParameterCount{.instance_id = instance_id()});
}

tresult PLUGIN_API Vst3PluginProxyImpl::getParameterInfo(
    int32 paramIndex,
    Steinberg::Vst::ParameterInfo& info /*out*/) {
    const GetParameterInfoResponse response = bridge.send_message(
        YaEditController::GetParameterInfo{.instance_id = instance_id(),
                                           .param_index = paramIndex,
                                           .info = info});

    info = response.updated_info;

    return response.result;
}

tresult PLUGIN_API Vst3PluginProxyImpl::getParamStringByValue(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue valueNormalized /*in*/,
    Steinberg::Vst::String128 string /*out*/) {
    const GetParamStringByValueResponse response =
        bridge.send_message(YaEditController::GetParamStringByValue{
            .instance_id = instance_id(),
            .id = id,
            .value_normalized = valueNormalized});

    std::copy(response.string.begin(), response.string.end(), string);
    string[response.string.size()] = 0;

    return response.result;
}

tresult PLUGIN_API Vst3PluginProxyImpl::getParamValueByString(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::TChar* string /*in*/,
    Steinberg::Vst::ParamValue& valueNormalized /*out*/) {
    const GetParamValueByStringResponse response =
        bridge.send_message(YaEditController::GetParamValueByString{
            .instance_id = instance_id(), .id = id, .string = string});

    valueNormalized = response.value_normalized;

    return response.result;
}

Steinberg::Vst::ParamValue PLUGIN_API
Vst3PluginProxyImpl::normalizedParamToPlain(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue valueNormalized) {
    return bridge.send_message(YaEditController::NormalizedParamToPlain{
        .instance_id = instance_id(),
        .id = id,
        .value_normalized = valueNormalized});
}

Steinberg::Vst::ParamValue PLUGIN_API
Vst3PluginProxyImpl::plainParamToNormalized(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue plainValue) {
    return bridge.send_message(YaEditController::PlainParamToNormalized{
        .instance_id = instance_id(), .id = id, .plain_value = plainValue});
}

Steinberg::Vst::ParamValue PLUGIN_API
Vst3PluginProxyImpl::getParamNormalized(Steinberg::Vst::ParamID id) {
    return bridge.send_message(YaEditController::GetParamNormalized{
        .instance_id = instance_id(), .id = id});
}

tresult PLUGIN_API
Vst3PluginProxyImpl::setParamNormalized(Steinberg::Vst::ParamID id,
                                        Steinberg::Vst::ParamValue value) {
    return bridge.send_message(YaEditController::SetParamNormalized{
        .instance_id = instance_id(), .id = id, .value = value});
}

tresult PLUGIN_API Vst3PluginProxyImpl::setComponentHandler(
    Steinberg::Vst::IComponentHandler* handler) {
    std::optional<Vst3ComponentHandlerProxy::ConstructArgs>
        component_handler_proxy_args = std::nullopt;
    if (handler) {
        // We'll store the pointer for when the plugin later makes a callback to
        // this component handler
        component_handler = handler;

        component_handler_proxy_args = Vst3ComponentHandlerProxy::ConstructArgs(
            component_handler, instance_id());
    } else {
        bridge.logger.log(
            "Null pointer passed to 'IEditController::setComponentHandler'");
    }

    return bridge.send_message(YaEditController::SetComponentHandler{
        .instance_id = instance_id(),
        .component_handler_proxy_args =
            std::move(component_handler_proxy_args)});
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
    // This `context` will likely be an `IHostApplication`. If it is, we
    // will store it here, and we'll proxy through all calls to it made from
    // the Wine side. Otherwise we'll still call `IPluginBase::initialize()`
    // but with a null pointer instead.
    host_application_context = context;

    std::optional<YaHostApplication::ConstructArgs>
        host_application_context_args = std::nullopt;
    if (host_application_context) {
        host_application_context_args = YaHostApplication::ConstructArgs(
            host_application_context, instance_id());
    } else {
        bridge.logger.log_unknown_interface(
            "In IPluginBase::initialize()",
            context ? std::optional(context->iid) : std::nullopt);
    }

    return bridge.send_message(
        YaPluginBase::Initialize{.instance_id = instance_id(),
                                 .host_application_context_args =
                                     std::move(host_application_context_args)});
}

tresult PLUGIN_API Vst3PluginProxyImpl::terminate() {
    return bridge.send_message(
        YaPluginBase::Terminate{.instance_id = instance_id()});
}
