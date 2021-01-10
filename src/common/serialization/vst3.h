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

#pragma once

#include <variant>

#include <bitsery/ext/std_variant.h>

#include "../configuration.h"
#include "../utils.h"
#include "common.h"
#include "vst3/component-handler-proxy.h"
#include "vst3/connection-point-proxy.h"
#include "vst3/context-menu-proxy.h"
#include "vst3/context-menu-target.h"
#include "vst3/host-context-proxy.h"
#include "vst3/plug-frame-proxy.h"
#include "vst3/plug-view-proxy.h"
#include "vst3/plugin-factory.h"
#include "vst3/plugin-proxy.h"

// Event handling for our VST3 plugins works slightly different from how we
// handle VST2 plugins. VST3 does not have a centralized event dispatching
// interface like VST2 does, and it uses a bunch of separate interfaces instead.
// Instead of having an a single event/result with accompanying payload values
// for both host -> plugin `dispatcher()` and plugin -> host `audioMaster()`
// calls, we'll send objects of type `T` that should receive a response of type
// `T::Response`, where all of the possible `T`s are stored in an
// `std::variant`. This way we communicate in a completely type safe way.

// TODO: If this approach works, maybe we can also refactor the VST2 handling to
//       do this since it's a bit safer and easier to read

// All messages for creating objects and calling interfaces on them are defined
// as part of the interfaces and implementations in `vst3/`

/**
 * Marker struct to indicate the other side (the plugin) should send a copy of
 * the configuration.
 */
struct WantsConfiguration {
    using Response = Configuration;

    template <typename S>
    void serialize(S&) {}
};

/**
 * When we send a control message from the plugin to the Wine VST host, this
 * encodes the information we request or the operation we want to perform. A
 * request of type `ControlRequest(T)` should send back a `T::Response`.
 */
using ControlRequest =
    std::variant<Vst3PlugViewProxy::Destruct,
                 Vst3PluginProxy::Construct,
                 Vst3PluginProxy::Destruct,
                 Vst3PluginProxy::SetState,
                 Vst3PluginProxy::GetState,
                 YaAudioPresentationLatency::SetAudioPresentationLatencySamples,
                 YaAutomationState::SetAutomationState,
                 YaConnectionPoint::Connect,
                 YaConnectionPoint::Disconnect,
                 YaConnectionPoint::Notify,
                 YaContextMenuTarget::ExecuteMenuItem,
                 YaEditController::SetComponentState,
                 YaEditController::GetParameterCount,
                 YaEditController::GetParameterInfo,
                 YaEditController::GetParamStringByValue,
                 YaEditController::GetParamValueByString,
                 YaEditController::NormalizedParamToPlain,
                 YaEditController::PlainParamToNormalized,
                 YaEditController::GetParamNormalized,
                 YaEditController::SetParamNormalized,
                 YaEditController::SetComponentHandler,
                 YaEditController::CreateView,
                 YaEditController2::SetKnobMode,
                 YaEditController2::OpenHelp,
                 YaEditController2::OpenAboutBox,
                 YaEditControllerHostEditing::BeginEditFromHost,
                 YaEditControllerHostEditing::EndEditFromHost,
                 YaKeyswitchController::GetKeyswitchCount,
                 YaKeyswitchController::GetKeyswitchInfo,
                 YaMidiMapping::GetMidiControllerAssignment,
                 YaNoteExpressionController::GetNoteExpressionCount,
                 YaNoteExpressionController::GetNoteExpressionInfo,
                 YaNoteExpressionController::GetNoteExpressionStringByValue,
                 YaNoteExpressionController::GetNoteExpressionValueByString,
                 YaParameterFinder::FindParameter,
                 YaPlugView::IsPlatformTypeSupported,
                 YaPlugView::Attached,
                 YaPlugView::Removed,
                 YaPlugView::OnWheel,
                 YaPlugView::OnKeyDown,
                 YaPlugView::OnKeyUp,
                 YaPlugView::GetSize,
                 YaPlugView::OnSize,
                 YaPlugView::OnFocus,
                 YaPlugView::SetFrame,
                 YaPlugView::CanResize,
                 YaPlugView::CheckSizeConstraint,
                 YaPluginBase::Initialize,
                 YaPluginBase::Terminate,
                 YaPluginFactory::Construct,
                 YaPluginFactory::SetHostContext,
                 YaProgramListData::ProgramDataSupported,
                 YaProgramListData::GetProgramData,
                 YaProgramListData::SetProgramData,
                 YaUnitData::UnitDataSupported,
                 YaUnitData::GetUnitData,
                 YaUnitData::SetUnitData,
                 YaUnitInfo::GetUnitCount,
                 YaUnitInfo::GetUnitInfo,
                 YaUnitInfo::GetProgramListCount,
                 YaUnitInfo::GetProgramListInfo,
                 YaUnitInfo::GetProgramName,
                 YaUnitInfo::GetProgramInfo,
                 YaUnitInfo::HasProgramPitchNames,
                 YaUnitInfo::GetProgramPitchName,
                 YaUnitInfo::GetSelectedUnit,
                 YaUnitInfo::SelectUnit,
                 YaUnitInfo::GetUnitByBus,
                 YaUnitInfo::SetUnitProgramData,
                 YaXmlRepresentationController::GetXmlRepresentationStream>;

template <typename S>
void serialize(S& s, ControlRequest& payload) {
    // All of the objects in `ControlRequest` should have their own
    // serialization function.
    s.ext(payload, bitsery::ext::StdVariant{});
}

/**
 * A subset of all functions a host can call on a plugin. These functions are
 * called from a hot loop every processing cycle, so we want a dedicated socket
 * for these for every plugin instance.
 */
using AudioProcessorRequest =
    std::variant<YaAudioProcessor::SetBusArrangements,
                 YaAudioProcessor::GetBusArrangement,
                 YaAudioProcessor::CanProcessSampleSize,
                 YaAudioProcessor::GetLatencySamples,
                 YaAudioProcessor::SetupProcessing,
                 YaAudioProcessor::SetProcessing,
                 YaAudioProcessor::Process,
                 YaAudioProcessor::GetTailSamples,
                 YaComponent::GetControllerClassId,
                 YaComponent::SetIoMode,
                 YaComponent::GetBusCount,
                 YaComponent::GetBusInfo,
                 YaComponent::GetRoutingInfo,
                 YaComponent::ActivateBus,
                 YaComponent::SetActive>;

template <typename S>
void serialize(S& s, AudioProcessorRequest& payload) {
    // All of the objects in `AudioProcessorRequest` should have their own
    // serialization function.
    s.ext(payload, bitsery::ext::StdVariant{});
}

/**
 * When we do a callback from the Wine VST host to the plugin, this encodes the
 * information we want or the operation we want to perform. A request of type
 * `CallbackRequest(T)` should send back a `T::Response`.
 */
using CallbackRequest = std::variant<Vst3ContextMenuProxy::Destruct,
                                     WantsConfiguration,
                                     YaComponentHandler::BeginEdit,
                                     YaComponentHandler::PerformEdit,
                                     YaComponentHandler::EndEdit,
                                     YaComponentHandler::RestartComponent,
                                     YaComponentHandler2::SetDirty,
                                     YaComponentHandler2::RequestOpenEditor,
                                     YaComponentHandler2::StartGroupEdit,
                                     YaComponentHandler2::FinishGroupEdit,
                                     YaComponentHandler3::CreateContextMenu,
                                     // Used when the host uses proxy objects,
                                     // and we have to route
                                     // `IConnectionPoint::notify` calls through
                                     // there
                                     YaConnectionPoint::Notify,
                                     YaContextMenu::GetItemCount,
                                     YaContextMenu::AddItem,
                                     YaContextMenu::RemoveItem,
                                     YaContextMenu::Popup,
                                     YaHostApplication::GetName,
                                     YaPlugFrame::ResizeView,
                                     YaUnitHandler::NotifyUnitSelection,
                                     YaUnitHandler::NotifyProgramListChange>;

template <typename S>
void serialize(S& s, CallbackRequest& payload) {
    // All of the objects in `CallbackRequest` should have their own
    // serialization function.
    s.ext(payload, bitsery::ext::StdVariant{});
}
