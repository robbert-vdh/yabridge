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

#include "component-handler-proxy.h"

#include <iostream>

Vst3ComponentHandlerProxyImpl::Vst3ComponentHandlerProxyImpl(
    Vst3Bridge& bridge,
    Vst3ComponentHandlerProxy::ConstructArgs&& args)
    : Vst3ComponentHandlerProxy(std::move(args)), bridge(bridge) {
    // The lifecycle of this object is managed together with that of the plugin
    // object instance this host context got passed to
}

tresult PLUGIN_API
Vst3ComponentHandlerProxyImpl::queryInterface(const Steinberg::TUID _iid,
                                              void** obj) {
    // TODO: Successful queries should also be logged
    const tresult result = Vst3ComponentHandlerProxy::queryInterface(_iid, obj);
    if (result != Steinberg::kResultOk) {
        bridge.logger.log_unknown_interface(
            "In IComponentHandler::queryInterface()",
            Steinberg::FUID::fromTUID(_iid));
    }

    return result;
}

tresult PLUGIN_API
Vst3ComponentHandlerProxyImpl::beginEdit(Steinberg::Vst::ParamID id) {
    return bridge.send_message(YaComponentHandler::BeginEdit{
        .owner_instance_id = owner_instance_id(), .id = id});
}

tresult PLUGIN_API Vst3ComponentHandlerProxyImpl::performEdit(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue valueNormalized) {
    return bridge.send_message(YaComponentHandler::PerformEdit{
        .owner_instance_id = owner_instance_id(),
        .id = id,
        .value_normalized = valueNormalized});
}

tresult PLUGIN_API
Vst3ComponentHandlerProxyImpl::endEdit(Steinberg::Vst::ParamID id) {
    return bridge.send_message(YaComponentHandler::EndEdit{
        .owner_instance_id = owner_instance_id(), .id = id});
}

tresult PLUGIN_API
Vst3ComponentHandlerProxyImpl::restartComponent(int32 flags) {
    return bridge.send_message(YaComponentHandler::RestartComponent{
        .owner_instance_id = owner_instance_id(), .flags = flags});
}

tresult PLUGIN_API Vst3ComponentHandlerProxyImpl::notifyUnitSelection(
    Steinberg::Vst::UnitID unitId) {
    return bridge.send_message(YaUnitHandler::NotifyUnitSelection{
        .owner_instance_id = owner_instance_id(), .unit_id = unitId});
}

tresult PLUGIN_API Vst3ComponentHandlerProxyImpl::notifyProgramListChange(
    Steinberg::Vst::ProgramListID listId,
    int32 programIndex) {
    return bridge.send_message(YaUnitHandler::NotifyProgramListChange{
        .owner_instance_id = owner_instance_id(),
        .list_id = listId,
        .program_index = programIndex});
}
