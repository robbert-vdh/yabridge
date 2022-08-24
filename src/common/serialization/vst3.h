// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2022 Robbert van der Helm
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

#include "../bitsery/ext/in-place-variant.h"

#include "../bitsery/ext/message-reference.h"
#include "../utils.h"
#include "common.h"
#include "vst3/component-handler-proxy.h"
#include "vst3/connection-point-proxy.h"
#include "vst3/context-menu-proxy.h"
#include "vst3/context-menu-target.h"
#include "vst3/host-context-proxy.h"
#include "vst3/plug-frame-proxy.h"
#include "vst3/plug-view-proxy.h"
#include "vst3/plugin-factory-proxy.h"
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
 * When we send a control message from the plugin to the Wine plugin host, this
 * encodes the information we request or the operation we want to perform. A
 * request of type `Vst3ControlRequest(T)` should send back a `T::Response`.
 */
using Vst3ControlRequest =
    std::variant<Vst3PluginFactoryProxy::Construct,
                 Vst3PlugViewProxy::Destruct,
                 Vst3PluginProxy::Construct,
                 Vst3PluginProxy::Destruct,
                 // This is actually part of `YaPluginBase`, but thanks to Waves
                 // we had to move this message to the main `Vst3PluginProxy`
                 // class
                 Vst3PluginProxy::Initialize,
                 // These are defined in both `IComponent` and `IEditController`
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
                 YaInfoListener::SetChannelContextInfos,
                 YaKeyswitchController::GetKeyswitchCount,
                 YaKeyswitchController::GetKeyswitchInfo,
                 YaMidiLearn::OnLiveMIDIControllerInput,
                 YaMidiMapping::GetMidiControllerAssignment,
                 YaNoteExpressionController::GetNoteExpressionCount,
                 YaNoteExpressionController::GetNoteExpressionInfo,
                 YaNoteExpressionController::GetNoteExpressionStringByValue,
                 YaNoteExpressionController::GetNoteExpressionValueByString,
                 YaNoteExpressionPhysicalUIMapping::GetNotePhysicalUIMapping,
                 YaParameterFinder::FindParameter,
                 YaParameterFunctionName::GetParameterIDFromFunctionName,
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
                 YaPlugViewContentScaleSupport::SetContentScaleFactor,
                 YaPluginBase::Terminate,
                 YaPluginFactory3::SetHostContext,
                 YaProcessContextRequirements::GetProcessContextRequirements,
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
void serialize(S& s, Vst3ControlRequest& payload) {
    // All of the objects in `Vst3ControlRequest` should have their own
    // serialization function
    s.ext(payload, bitsery::ext::InPlaceVariant{});
}

/**
 * A subset of all functions a host can call on a plugin. These functions are
 * called from a hot loop every processing cycle, so we want a dedicated socket
 * for these for every plugin instance.
 *
 * We use a separate struct for this so we can keep the
 * `YaAudioProcessor::Process` object, which also contains the entire audio
 * processing data struct, alive as a thread local static object on the Wine
 * side, and as a regular field in `Vst3PluginProxyImpl` on the plugin side. In
 * our variant we then store a `MessageReference<T>` that points to this object,
 * and we'll do some magic to be able to serialize and deserialize this object
 * without needing to create copies. See `MessageReference<T>` and
 * `bitsery::ext::MessageReference<T>` for more information.
 */
struct Vst3AudioProcessorRequest {
    Vst3AudioProcessorRequest() {}

    /**
     * Initialize the variant with an object. In `Vst3Sockets::send_message()`
     * the object gets implicitly converted to the this variant.
     */
    template <typename T>
    Vst3AudioProcessorRequest(T request) : payload(std::move(request)) {}

    using Payload =
        std::variant<YaAudioProcessor::SetBusArrangements,
                     YaAudioProcessor::GetBusArrangement,
                     YaAudioProcessor::CanProcessSampleSize,
                     YaAudioProcessor::GetLatencySamples,
                     YaAudioProcessor::SetupProcessing,
                     YaAudioProcessor::SetProcessing,
                     // The actual value for this will be stored in the
                     // `process_request` field. That way we don't have to
                     // destroy the object (and deallocate all vectors in it) on
                     // the Wine side during every processing cycle.
                     MessageReference<YaAudioProcessor::Process>,
                     YaAudioProcessor::GetTailSamples,
                     YaComponent::GetControllerClassId,
                     YaComponent::SetIoMode,
                     YaComponent::GetBusCount,
                     YaComponent::GetBusInfo,
                     YaComponent::GetRoutingInfo,
                     YaComponent::ActivateBus,
                     YaComponent::SetActive,
                     YaPrefetchableSupport::GetPrefetchableSupport>;

    Payload payload;

    template <typename S>
    void serialize(S& s) {
        s.ext(
            payload,
            bitsery::ext::InPlaceVariant{
                [&](S& s,
                    MessageReference<YaAudioProcessor::Process>& request_ref) {
                    // When serializing this reference we'll read the data
                    // directly from the referred to object. During
                    // deserializing we'll deserialize into the persistent and
                    // thread local `process_request` object (see
                    // `Vst3Sockets::add_audio_processor_and_listen`) and then
                    // reassign the reference to point to that boject.
                    s.ext(request_ref,
                          bitsery::ext::MessageReference(process_request_));
                },
                [](S& s, auto& request) { s.object(request); }});
    }

    /**
     * Used for deserializing the `MessageReference<YaAudioProcessor::Process>`
     * variant. When we encounter this variant, we'll actually deserialize the
     * object into this object, and we'll then reassign the reference to point
     * to this object. That way we can keep it around as a thread local object
     * to prevent unnecessary allocations.
     */
    std::optional<YaAudioProcessor::Process> process_request_;
};

/**
 * Fetch the `std::variant<>` from an audio processor request object. This will
 * let us use our regular, simple function call dispatch code, but we can still
 * store the process data in a separate field (to reduce allocations).
 *
 * This overloads the `get_request_variant()` function from
 * `../communication/common.h`.
 *
 * @overload
 */
inline Vst3AudioProcessorRequest::Payload& get_request_variant(
    Vst3AudioProcessorRequest& request) noexcept {
    return request.payload;
}

/**
 * When we do a callback from the Wine plugin host to the plugin, this encodes
 * the information we want or the operation we want to perform. A request of
 * type `Vst3CallbackRequest(T)` should send back a `T::Response`.
 */
using Vst3CallbackRequest =
    std::variant<Vst3ContextMenuProxy::Destruct,
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
                 YaComponentHandlerBusActivation::RequestBusActivation,
                 // Used when the host uses proxy objects,
                 // and we have to route
                 // `IConnectionPoint::notify` calls through
                 // there
                 YaConnectionPoint::Notify,
                 YaContextMenu::AddItem,
                 YaContextMenu::RemoveItem,
                 YaContextMenu::Popup,
                 YaContextMenuTarget::ExecuteMenuItem,
                 YaHostApplication::GetName,
                 YaPlugFrame::ResizeView,
                 YaPlugInterfaceSupport::IsPlugInterfaceSupported,
                 YaProgress::Start,
                 YaProgress::Update,
                 YaProgress::Finish,
                 YaUnitHandler::NotifyUnitSelection,
                 YaUnitHandler::NotifyProgramListChange,
                 YaUnitHandler2::NotifyUnitByBusChange>;

template <typename S>
void serialize(S& s, Vst3CallbackRequest& payload) {
    // All of the objects in `Vst3CallbackRequest` should have their own
    // serialization function.
    s.ext(payload, bitsery::ext::InPlaceVariant{});
}
