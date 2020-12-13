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

    return bridge
        .send_message(YaComponent::Initialize{
            .instance_id = arguments.instance_id,
            .host_application_context_args =
                std::move(host_application_context_args)})
        .native();
}

tresult PLUGIN_API YaComponentPluginImpl::terminate() {
    return bridge
        .send_message(
            YaComponent::Terminate{.instance_id = arguments.instance_id})
        .native();
}

tresult PLUGIN_API
YaComponentPluginImpl::setIoMode(Steinberg::Vst::IoMode mode) {
    // TODO: Implement
    bridge.logger.log("TODO: IComponent::setIoMode()");
    return Steinberg::kNotImplemented;
}

int32 PLUGIN_API
YaComponentPluginImpl::getBusCount(Steinberg::Vst::MediaType type,
                                   Steinberg::Vst::BusDirection dir) {
    // TODO: Implement
    bridge.logger.log("TODO: IComponent::getBusCount()");
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API
YaComponentPluginImpl::getBusInfo(Steinberg::Vst::MediaType type,
                                  Steinberg::Vst::BusDirection dir,
                                  int32 index,
                                  Steinberg::Vst::BusInfo& bus /*out*/) {
    // TODO: Implement
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API YaComponentPluginImpl::getRoutingInfo(
    Steinberg::Vst::RoutingInfo& inInfo,
    Steinberg::Vst::RoutingInfo& outInfo /*out*/) {
    // TODO: Implement
    bridge.logger.log("TODO: IComponent::getRoutingInfo()");
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API
YaComponentPluginImpl::activateBus(Steinberg::Vst::MediaType type,
                                   Steinberg::Vst::BusDirection dir,
                                   int32 index,
                                   TBool state) {
    // TODO: Implement
    bridge.logger.log("TODO: IComponent::activateBus()");
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API YaComponentPluginImpl::setActive(TBool state) {
    // TODO: Implement
    bridge.logger.log("TODO: IComponent::setActive()");
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API YaComponentPluginImpl::setState(Steinberg::IBStream* state) {
    // TODO: Implement
    bridge.logger.log("TODO: IComponent::setState()");
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API YaComponentPluginImpl::getState(Steinberg::IBStream* state) {
    // TODO: Implement
    bridge.logger.log("TODO: IComponent::getState()");
    return Steinberg::kNotImplemented;
}
