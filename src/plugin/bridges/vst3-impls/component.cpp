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
    : YaComponent(std::move(args)), bridge(bridge) {}

YaComponentPluginImpl::~YaComponentPluginImpl() {
    bridge.send_message(
        YaComponent::Destruct{.instance_id = arguments.instance_id});
}

tresult PLUGIN_API
YaComponentPluginImpl::queryInterface(const ::Steinberg::TUID _iid,
                                      void** obj) {
    // TODO: Log when this fails on debug level 1, and on debug level 2 also log
    //       successful queries. This behaviour should be implemented for all
    //       interfaces.
    return YaComponent::queryInterface(_iid, obj);
}

tresult PLUGIN_API YaComponentPluginImpl::initialize(FUnknown* context) {
    // TODO: Implement
    return Steinberg::kNotImplemented;
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
    return Steinberg::kNotImplemented;
}

int32 PLUGIN_API
YaComponentPluginImpl::getBusCount(Steinberg::Vst::MediaType type,
                                   Steinberg::Vst::BusDirection dir) {
    // TODO: Implement
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
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API
YaComponentPluginImpl::activateBus(Steinberg::Vst::MediaType type,
                                   Steinberg::Vst::BusDirection dir,
                                   int32 index,
                                   TBool state) {
    // TODO: Implement
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API YaComponentPluginImpl::setActive(TBool state) {
    // TODO: Implement
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API YaComponentPluginImpl::setState(Steinberg::IBStream* state) {
    // TODO: Implement
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API YaComponentPluginImpl::getState(Steinberg::IBStream* state) {
    // TODO: Implement
    return Steinberg::kNotImplemented;
}
