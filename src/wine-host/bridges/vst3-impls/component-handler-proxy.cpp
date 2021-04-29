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

#include "component-handler-proxy.h"

#include <iostream>

#include "context-menu-proxy.h"

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
    const tresult result = Vst3ComponentHandlerProxy::queryInterface(_iid, obj);
    bridge.logger.log_query_interface("In IComponentHandler::queryInterface()",
                                      result, Steinberg::FUID::fromTUID(_iid));

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
    return bridge.send_mutually_recursive_message(
        YaComponentHandler::RestartComponent{
            .owner_instance_id = owner_instance_id(), .flags = flags});
}

tresult PLUGIN_API Vst3ComponentHandlerProxyImpl::setDirty(TBool state) {
    return bridge.send_message(YaComponentHandler2::SetDirty{
        .owner_instance_id = owner_instance_id(), .state = state});
}

tresult PLUGIN_API
Vst3ComponentHandlerProxyImpl::requestOpenEditor(Steinberg::FIDString name) {
    if (name) {
        return bridge.send_message(YaComponentHandler2::RequestOpenEditor{
            .owner_instance_id = owner_instance_id(), .name = name});
    } else {
        std::cerr << "WARNING: Null pointer passed to "
                     "IComponentHandler2::requestOpenEditor()"
                  << std::endl;
        return Steinberg::kInvalidArgument;
    }
}

tresult PLUGIN_API Vst3ComponentHandlerProxyImpl::startGroupEdit() {
    return bridge.send_message(YaComponentHandler2::StartGroupEdit{
        .owner_instance_id = owner_instance_id()});
}

tresult PLUGIN_API Vst3ComponentHandlerProxyImpl::finishGroupEdit() {
    return bridge.send_message(YaComponentHandler2::FinishGroupEdit{
        .owner_instance_id = owner_instance_id()});
}

Steinberg::Vst::IContextMenu* PLUGIN_API
Vst3ComponentHandlerProxyImpl::createContextMenu(
    Steinberg::IPlugView* /*plugView*/,
    const Steinberg::Vst::ParamID* paramID) {
    // XXX: The does do not make it clear what `paramID` is, so my assumption
    //      that it really is a pointer to a parameter ID. I'll assume that 'the
    //      parameter being zero' was a typo and that they mean passign a null
    //      pointer.
    CreateContextMenuResponse response =
        bridge.send_message(YaComponentHandler3::CreateContextMenu{
            .owner_instance_id = owner_instance_id(),
            .param_id = (paramID ? std::optional(*paramID) : std::nullopt)});

    if (response.context_menu_args) {
        return new Vst3ContextMenuProxyImpl(
            bridge, std::move(*response.context_menu_args));
    } else {
        return nullptr;
    }
}

tresult PLUGIN_API Vst3ComponentHandlerProxyImpl::requestBusActivation(
    Steinberg::Vst::MediaType type,
    Steinberg::Vst::BusDirection dir,
    int32 index,
    TBool state) {
    return bridge.send_message(
        YaComponentHandlerBusActivation::RequestBusActivation{
            .owner_instance_id = owner_instance_id(),
            .type = type,
            .dir = dir,
            .index = index,
            .state = state});
}

tresult PLUGIN_API Vst3ComponentHandlerProxyImpl::start(
    ProgressType type,
    const Steinberg::tchar* optionalDescription,
    ID& outID) {
    const StartResponse response = bridge.send_message(YaProgress::Start{
        .owner_instance_id = owner_instance_id(),
        .type = type,
        .optional_description =
            (optionalDescription
                 ? std::optional<std::u16string>(
                       tchar_pointer_to_u16string(optionalDescription))
                 : std::nullopt)});

    outID = response.out_id;

    return response.result;
}

tresult PLUGIN_API
Vst3ComponentHandlerProxyImpl::update(ID id,
                                      Steinberg::Vst::ParamValue normValue) {
    return bridge.send_message(
        YaProgress::Update{.owner_instance_id = owner_instance_id(),
                           .id = id,
                           .norm_value = normValue});
}

tresult PLUGIN_API Vst3ComponentHandlerProxyImpl::finish(ID id) {
    return bridge.send_message(
        YaProgress::Finish{.owner_instance_id = owner_instance_id(), .id = id});
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

tresult PLUGIN_API Vst3ComponentHandlerProxyImpl::notifyUnitByBusChange() {
    return bridge.send_message(YaUnitHandler2::NotifyUnitByBusChange{
        .owner_instance_id = owner_instance_id()});
}
