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

#include "component.h"

YaComponentPluginImpl::YaComponentPluginImpl(Vst3PluginBridge& bridge,
                                             YaComponent::ConstructArgs&& args)
    : YaComponent(std::move(args)), bridge(bridge) {
    bridge.register_component(arguments.instance_id, *this);
}

YaComponentPluginImpl::~YaComponentPluginImpl() {
    bridge.send_message(
        YaComponent::Destruct{.instance_id = arguments.instance_id});
    bridge.unregister_component(arguments.instance_id);
}

tresult PLUGIN_API
YaComponentPluginImpl::queryInterface(const Steinberg::TUID _iid, void** obj) {
    // TODO: Successful queries should also be logged
    const tresult result = YaComponent::queryInterface(_iid, obj);
    if (result != Steinberg::kResultOk) {
        bridge.logger.log_unknown_interface("In IComponent::queryInterface()",
                                            Steinberg::FUID::fromTUID(_iid));
    }

    return result;
}

tresult PLUGIN_API YaComponentPluginImpl::initialize(FUnknown* context) {
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
        YaComponent::Initialize{.instance_id = arguments.instance_id,
                                .host_application_context_args =
                                    std::move(host_application_context_args)});
}

tresult PLUGIN_API YaComponentPluginImpl::terminate() {
    return bridge.send_message(
        YaComponent::Terminate{.instance_id = arguments.instance_id});
}

tresult PLUGIN_API
YaComponentPluginImpl::setIoMode(Steinberg::Vst::IoMode mode) {
    return bridge.send_message(YaComponent::SetIoMode{
        .instance_id = arguments.instance_id, .mode = mode});
}

int32 PLUGIN_API
YaComponentPluginImpl::getBusCount(Steinberg::Vst::MediaType type,
                                   Steinberg::Vst::BusDirection dir) {
    return bridge.send_message(YaComponent::GetBusCount{
        .instance_id = arguments.instance_id, .type = type, .dir = dir});
}

tresult PLUGIN_API
YaComponentPluginImpl::getBusInfo(Steinberg::Vst::MediaType type,
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

tresult PLUGIN_API YaComponentPluginImpl::getRoutingInfo(
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
YaComponentPluginImpl::activateBus(Steinberg::Vst::MediaType type,
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

tresult PLUGIN_API YaComponentPluginImpl::setActive(TBool state) {
    return bridge.send_message(YaComponent::SetActive{
        .instance_id = arguments.instance_id, .state = state});
}

tresult PLUGIN_API YaComponentPluginImpl::setState(Steinberg::IBStream* state) {
    return bridge.send_message(YaComponent::SetState{
        .instance_id = arguments.instance_id, .state = state});
}

tresult PLUGIN_API YaComponentPluginImpl::getState(Steinberg::IBStream* state) {
    const GetStateResponse response = bridge.send_message(
        YaComponent::GetState{.instance_id = arguments.instance_id});

    assert(response.updated_state.write_back(state) == Steinberg::kResultOk);

    return response.result;
}

tresult PLUGIN_API YaComponentPluginImpl::setBusArrangements(
    Steinberg::Vst::SpeakerArrangement* inputs,
    int32 numIns,
    Steinberg::Vst::SpeakerArrangement* outputs,
    int32 numOuts) {
    assert(inputs && outputs);
    return bridge.send_message(YaComponent::SetBusArrangements{
        .instance_id = arguments.instance_id,
        .inputs = std::vector<Steinberg::Vst::SpeakerArrangement>(
            inputs, &inputs[numIns]),
        .num_ins = numIns,
        .outputs = std::vector<Steinberg::Vst::SpeakerArrangement>(
            outputs, &outputs[numOuts]),
        .num_outs = numOuts,
    });
}

tresult PLUGIN_API YaComponentPluginImpl::getBusArrangement(
    Steinberg::Vst::BusDirection dir,
    int32 index,
    Steinberg::Vst::SpeakerArrangement& arr) {
    const GetBusArrangementResponse response = bridge.send_message(
        YaComponent::GetBusArrangement{.instance_id = arguments.instance_id,
                                       .dir = dir,
                                       .index = index,
                                       .arr = arr});

    arr = response.updated_arr;

    return response.result;
}

tresult PLUGIN_API
YaComponentPluginImpl::canProcessSampleSize(int32 symbolicSampleSize) {
    return bridge.send_message(YaComponent::CanProcessSampleSize{
        .instance_id = arguments.instance_id,
        .symbolic_sample_size = symbolicSampleSize});
}

uint32 PLUGIN_API YaComponentPluginImpl::getLatencySamples() {
    return bridge.send_message(
        YaComponent::GetLatencySamples{.instance_id = arguments.instance_id});
}

tresult PLUGIN_API
YaComponentPluginImpl::setupProcessing(Steinberg::Vst::ProcessSetup& setup) {
    return bridge.send_message(YaComponent::SetupProcessing{
        .instance_id = arguments.instance_id, .setup = setup});
}

tresult PLUGIN_API YaComponentPluginImpl::setProcessing(TBool state) {
    return bridge.send_message(YaComponent::SetProcessing{
        .instance_id = arguments.instance_id, .state = state});
}

tresult PLUGIN_API
YaComponentPluginImpl::process(Steinberg::Vst::ProcessData& data) {
    // TODO: Implement
    bridge.logger.log("TODO: IAudioProcessor::process()");
    return Steinberg::kNotImplemented;
}

uint32 PLUGIN_API YaComponentPluginImpl::getTailSamples() {
    return bridge.send_message(
        YaComponent::GetTailSamples{.instance_id = arguments.instance_id});
}
