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

#include "plugin-proxy.h"

Vst3PluginProxy::ConstructArgs::ConstructArgs() {}

Vst3PluginProxy::ConstructArgs::ConstructArgs(
    Steinberg::IPtr<Steinberg::FUnknown> object,
    size_t instance_id)
    : instance_id(instance_id),
      audio_presentation_latency_args(object),
      audio_processor_args(object),
      automation_state_args(object),
      component_args(object),
      connection_point_args(object),
      edit_controller_args(object),
      edit_controller_2_args(object),
      edit_controller_host_editing_args(object),
      info_listener_args(object),
      keyswitch_controller_args(object),
      midi_mapping_args(object),
      note_expression_controller_args(object),
      plugin_base_args(object),
      prefetchable_support_args(object),
      program_list_data_args(object),
      unit_data_args(object),
      unit_info_args(object),
      xml_representation_controller_args(object) {}

Vst3PluginProxy::Vst3PluginProxy(const ConstructArgs&& args)
    : YaAudioPresentationLatency(
          std::move(args.audio_presentation_latency_args)),
      YaAudioProcessor(std::move(args.audio_processor_args)),
      YaAutomationState(std::move(args.automation_state_args)),
      YaComponent(std::move(args.component_args)),
      YaConnectionPoint(std::move(args.connection_point_args)),
      YaEditController(std::move(args.edit_controller_args)),
      YaEditController2(std::move(args.edit_controller_2_args)),
      YaEditControllerHostEditing(
          std::move(args.edit_controller_host_editing_args)),
      YaInfoListener(std::move(args.info_listener_args)),
      YaKeyswitchController(std::move(args.keyswitch_controller_args)),
      YaMidiMapping(std::move(args.midi_mapping_args)),
      YaNoteExpressionController(
          std::move(args.note_expression_controller_args)),
      YaNoteExpressionPhysicalUIMapping(
          std::move(args.note_expression_physical_ui_mapping_args)),
      YaPluginBase(std::move(args.plugin_base_args)),
      YaPrefetchableSupport(std::move(args.prefetchable_support_args)),
      YaProgramListData(std::move(args.program_list_data_args)),
      YaUnitData(std::move(args.unit_data_args)),
      YaUnitInfo(std::move(args.unit_info_args)),
      YaXmlRepresentationController(
          std::move(args.xml_representation_controller_args)),
      arguments(std::move(args)){FUNKNOWN_CTOR}

      Vst3PluginProxy::~Vst3PluginProxy() {
    FUNKNOWN_DTOR
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
IMPLEMENT_REFCOUNT(Vst3PluginProxy)
#pragma GCC diagnostic pop

tresult PLUGIN_API Vst3PluginProxy::queryInterface(Steinberg::FIDString _iid,
                                                   void** obj) {
    if (YaPluginBase::supported()) {
        // We had to expand the macro here because we need to cast through
        // `YaPluginBase`, since `IpluginBase` is also a base of `IComponent`
        if (Steinberg::FUnknownPrivate ::iidEqual(_iid,
                                                  Steinberg::FUnknown::iid)) {
            addRef();
            *obj = static_cast<Steinberg ::IPluginBase*>(
                static_cast<YaPluginBase*>(this));
            return ::Steinberg ::kResultOk;
        }
        if (Steinberg::FUnknownPrivate ::iidEqual(
                _iid, Steinberg::IPluginBase::iid)) {
            addRef();
            *obj = static_cast<Steinberg ::IPluginBase*>(
                static_cast<YaPluginBase*>(this));
            return ::Steinberg ::kResultOk;
        }
    }
    if (YaAudioPresentationLatency::supported()) {
        QUERY_INTERFACE(_iid, obj,
                        Steinberg::Vst::IAudioPresentationLatency::iid,
                        Steinberg::Vst::IAudioPresentationLatency)
    }
    if (YaAudioProcessor::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IAudioProcessor::iid,
                        Steinberg::Vst::IAudioProcessor)
    }
    if (YaAutomationState::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IAutomationState::iid,
                        Steinberg::Vst::IAutomationState)
    }
    if (YaComponent::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IComponent::iid,
                        Steinberg::Vst::IComponent)
    }
    if (YaConnectionPoint::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IConnectionPoint::iid,
                        Steinberg::Vst::IConnectionPoint)
    }
    if (YaEditController::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IEditController::iid,
                        Steinberg::Vst::IEditController)
    }
    if (YaEditController2::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IEditController2::iid,
                        Steinberg::Vst::IEditController2)
    }
    if (YaEditControllerHostEditing::supported()) {
        QUERY_INTERFACE(_iid, obj,
                        Steinberg::Vst::IEditControllerHostEditing::iid,
                        Steinberg::Vst::IEditControllerHostEditing)
    }
    if (YaInfoListener::supported()) {
        QUERY_INTERFACE(_iid, obj,
                        Steinberg::Vst::ChannelContext::IInfoListener::iid,
                        Steinberg::Vst::ChannelContext::IInfoListener)
    }
    if (YaKeyswitchController::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IKeyswitchController::iid,
                        Steinberg::Vst::IKeyswitchController)
    }
    if (YaMidiMapping::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IMidiMapping::iid,
                        Steinberg::Vst::IMidiMapping)
    }
    if (YaNoteExpressionController::supported()) {
        QUERY_INTERFACE(_iid, obj,
                        Steinberg::Vst::INoteExpressionController::iid,
                        Steinberg::Vst::INoteExpressionController)
    }
    if (YaNoteExpressionPhysicalUIMapping::supported()) {
        QUERY_INTERFACE(_iid, obj,
                        Steinberg::Vst::INoteExpressionPhysicalUIMapping::iid,
                        Steinberg::Vst::INoteExpressionPhysicalUIMapping)
    }
    if (YaPrefetchableSupport::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IPrefetchableSupport::iid,
                        Steinberg::Vst::IPrefetchableSupport)
    }
    if (YaProgramListData::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IProgramListData::iid,
                        Steinberg::Vst::IProgramListData)
    }
    if (YaUnitData::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IUnitData::iid,
                        Steinberg::Vst::IUnitData)
    }
    if (YaUnitInfo::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IUnitInfo::iid,
                        Steinberg::Vst::IUnitInfo)
    }
    if (YaXmlRepresentationController::supported()) {
        QUERY_INTERFACE(_iid, obj,
                        Steinberg::Vst::IXmlRepresentationController::iid,
                        Steinberg::Vst::IXmlRepresentationController)
    }

    *obj = nullptr;
    return Steinberg::kNoInterface;
}
