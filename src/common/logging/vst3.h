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

#include <sstream>

#include "../serialization/vst3.h"
#include "common.h"

/**
 * Wraps around `Logger` to provide VST3-specific logging functionality for
 * debugging plugins. This way we can have all the complex initialisation be
 * performed in one place.
 */
class Vst3Logger {
   public:
    Vst3Logger(Logger& generic_logger);

    /**
     * @see Logger::log
     */
    inline void log(const std::string& message) { logger_.log(message); }

    /**
     * Log calls to `FUnknown::queryInterface`. This will separately log about
     * successful queries, queries for interfaces the object did not implement,
     * and queries for interfaces we do not implement depending on the verbosity
     * level. In case we could not get a FUID (because of null pointers, for
     * instance), `std::nullopt` should be passed.
     *
     * NOTE: We're passing a `const char*` here instead of a `const
     *       std::string&` because that will still allocate for longer strings
     *       because `std::string` isn't constexpr yet.  Most calls to this
     *       function won't print anything, so we should make sure that calling
     *       it doesn't add unnecessary overhead.
     */
    void log_query_interface(const char* where,
                             tresult result,
                             const std::optional<Steinberg::FUID>& uid);

    // For every object we send using the `TypedMessageHandler` we have
    // overloads that print information about the request and the response. The
    // boolean flag here indicates whether the request was initiated on the host
    // side (what we'll call a control message). `log_response()` should only be
    // called if the corresponding `log_request()` call returned `true`. This
    // way we can filter out the log message for the response together with the
    // request.

    bool log_request(bool is_host_plugin,
                     const Vst3PluginFactoryProxy::Construct&);
    bool log_request(bool is_host_plugin, const Vst3PlugViewProxy::Destruct&);
    bool log_request(bool is_host_plugin, const Vst3PluginProxy::Construct&);
    bool log_request(bool is_host_plugin, const Vst3PluginProxy::Destruct&);
    bool log_request(bool is_host_plugin, const Vst3PluginProxy::Initialize&);
    bool log_request(bool is_host_plugin, const Vst3PluginProxy::SetState&);
    bool log_request(bool is_host_plugin, const Vst3PluginProxy::GetState&);
    bool log_request(
        bool is_host_plugin,
        const YaAudioPresentationLatency::SetAudioPresentationLatencySamples&);
    bool log_request(bool is_host_plugin,
                     const YaAutomationState::SetAutomationState&);
    bool log_request(bool is_host_plugin, const YaConnectionPoint::Connect&);
    bool log_request(bool is_host_plugin, const YaConnectionPoint::Disconnect&);
    bool log_request(bool is_host_plugin, const YaConnectionPoint::Notify&);
    bool log_request(bool is_host_plugin,
                     const YaContextMenuTarget::ExecuteMenuItem&);
    bool log_request(bool is_host_plugin,
                     const YaEditController::SetComponentState&);
    bool log_request(bool is_host_plugin,
                     const YaEditController::GetParameterCount&);
    bool log_request(bool is_host_plugin,
                     const YaEditController::GetParameterInfo&);
    bool log_request(bool is_host_plugin,
                     const YaEditController::GetParamStringByValue&);
    bool log_request(bool is_host_plugin,
                     const YaEditController::GetParamValueByString&);
    bool log_request(bool is_host_plugin,
                     const YaEditController::NormalizedParamToPlain&);
    bool log_request(bool is_host_plugin,
                     const YaEditController::PlainParamToNormalized&);
    bool log_request(bool is_host_plugin,
                     const YaEditController::GetParamNormalized&);
    bool log_request(bool is_host_plugin,
                     const YaEditController::SetParamNormalized&);
    bool log_request(bool is_host_plugin,
                     const YaEditController::SetComponentHandler&);
    bool log_request(bool is_host_plugin, const YaEditController::CreateView&);
    bool log_request(bool is_host_plugin,
                     const YaEditController2::SetKnobMode&);
    bool log_request(bool is_host_plugin, const YaEditController2::OpenHelp&);
    bool log_request(bool is_host_plugin,
                     const YaEditController2::OpenAboutBox&);
    bool log_request(bool is_host_plugin,
                     const YaEditControllerHostEditing::BeginEditFromHost&);
    bool log_request(bool is_host_plugin,
                     const YaEditControllerHostEditing::EndEditFromHost&);
    bool log_request(bool is_host_plugin,
                     const YaInfoListener::SetChannelContextInfos&);
    bool log_request(bool is_host_plugin,
                     const YaKeyswitchController::GetKeyswitchCount&);
    bool log_request(bool is_host_plugin,
                     const YaKeyswitchController::GetKeyswitchInfo&);
    bool log_request(bool is_host_plugin,
                     const YaMidiLearn::OnLiveMIDIControllerInput&);
    bool log_request(bool is_host_plugin,
                     const YaMidiMapping::GetMidiControllerAssignment&);
    bool log_request(bool is_host_plugin,
                     const YaNoteExpressionController::GetNoteExpressionCount&);
    bool log_request(bool is_host_plugin,
                     const YaNoteExpressionController::GetNoteExpressionInfo&);
    bool log_request(
        bool is_host_plugin,
        const YaNoteExpressionController::GetNoteExpressionStringByValue&);
    bool log_request(
        bool is_host_plugin,
        const YaNoteExpressionController::GetNoteExpressionValueByString&);
    bool log_request(
        bool is_host_plugin,
        const YaNoteExpressionPhysicalUIMapping::GetNotePhysicalUIMapping&);
    bool log_request(bool is_host_plugin,
                     const YaParameterFinder::FindParameter&);
    bool log_request(
        bool is_host_plugin,
        const YaParameterFunctionName::GetParameterIDFromFunctionName&);
    bool log_request(bool is_host_plugin,
                     const YaPlugView::IsPlatformTypeSupported&);
    bool log_request(bool is_host_plugin, const YaPlugView::Attached&);
    bool log_request(bool is_host_plugin, const YaPlugView::Removed&);
    bool log_request(bool is_host_plugin, const YaPlugView::OnWheel&);
    bool log_request(bool is_host_plugin, const YaPlugView::OnKeyDown&);
    bool log_request(bool is_host_plugin, const YaPlugView::OnKeyUp&);
    bool log_request(bool is_host_plugin, const YaPlugView::GetSize&);
    bool log_request(bool is_host_plugin, const YaPlugView::OnSize&);
    bool log_request(bool is_host_plugin, const YaPlugView::OnFocus&);
    bool log_request(bool is_host_plugin, const YaPlugView::SetFrame&);
    bool log_request(bool is_host_plugin, const YaPlugView::CanResize&);
    bool log_request(bool is_host_plugin,
                     const YaPlugView::CheckSizeConstraint&);
    bool log_request(
        bool is_host_plugin,
        const YaPlugViewContentScaleSupport::SetContentScaleFactor&);
    bool log_request(bool is_host_plugin, const YaPluginBase::Terminate&);
    bool log_request(bool is_host_plugin,
                     const YaPluginFactory3::SetHostContext&);
    bool log_request(
        bool is_host_plugin,
        const YaProcessContextRequirements::GetProcessContextRequirements&);
    bool log_request(bool is_host_plugin,
                     const YaProgramListData::ProgramDataSupported&);
    bool log_request(bool is_host_plugin,
                     const YaProgramListData::GetProgramData&);
    bool log_request(bool is_host_plugin,
                     const YaProgramListData::SetProgramData&);
    bool log_request(bool is_host_plugin, const YaUnitData::UnitDataSupported&);
    bool log_request(bool is_host_plugin, const YaUnitData::GetUnitData&);
    bool log_request(bool is_host_plugin, const YaUnitData::SetUnitData&);
    bool log_request(bool is_host_plugin, const YaUnitInfo::GetUnitCount&);
    bool log_request(bool is_host_plugin, const YaUnitInfo::GetUnitInfo&);
    bool log_request(bool is_host_plugin,
                     const YaUnitInfo::GetProgramListCount&);
    bool log_request(bool is_host_plugin,
                     const YaUnitInfo::GetProgramListInfo&);
    bool log_request(bool is_host_plugin, const YaUnitInfo::GetProgramName&);
    bool log_request(bool is_host_plugin, const YaUnitInfo::GetProgramInfo&);
    bool log_request(bool is_host_plugin,
                     const YaUnitInfo::HasProgramPitchNames&);
    bool log_request(bool is_host_plugin,
                     const YaUnitInfo::GetProgramPitchName&);
    bool log_request(bool is_host_plugin, const YaUnitInfo::GetSelectedUnit&);
    bool log_request(bool is_host_plugin, const YaUnitInfo::SelectUnit&);
    bool log_request(bool is_host_plugin, const YaUnitInfo::GetUnitByBus&);
    bool log_request(bool is_host_plugin,
                     const YaUnitInfo::SetUnitProgramData&);
    bool log_request(
        bool is_host_plugin,
        const YaXmlRepresentationController::GetXmlRepresentationStream&);

    bool log_request(bool is_host_plugin,
                     const YaAudioProcessor::SetBusArrangements&);
    bool log_request(bool is_host_plugin,
                     const YaAudioProcessor::GetBusArrangement&);
    bool log_request(bool is_host_plugin,
                     const YaAudioProcessor::CanProcessSampleSize&);
    bool log_request(bool is_host_plugin,
                     const YaAudioProcessor::GetLatencySamples&);
    bool log_request(bool is_host_plugin,
                     const YaAudioProcessor::SetupProcessing&);
    bool log_request(bool is_host_plugin,
                     const YaAudioProcessor::SetProcessing&);
    bool log_request(bool is_host_plugin,
                     const MessageReference<YaAudioProcessor::Process>&);
    bool log_request(bool is_host_plugin,
                     const YaAudioProcessor::GetTailSamples&);
    bool log_request(bool is_host_plugin,
                     const YaComponent::GetControllerClassId&);
    bool log_request(bool is_host_plugin, const YaComponent::SetIoMode&);
    bool log_request(bool is_host_plugin, const YaComponent::GetBusCount&);
    bool log_request(bool is_host_plugin, const YaComponent::GetBusInfo&);
    bool log_request(bool is_host_plugin, const YaComponent::GetRoutingInfo&);
    bool log_request(bool is_host_plugin, const YaComponent::ActivateBus&);
    bool log_request(bool is_host_plugin, const YaComponent::SetActive&);
    bool log_request(bool is_host_plugin,
                     const YaPrefetchableSupport::GetPrefetchableSupport&);

    bool log_request(bool is_host_plugin,
                     const Vst3ContextMenuProxy::Destruct&);
    bool log_request(bool is_host_plugin, const WantsConfiguration&);
    bool log_request(bool is_host_plugin, const YaComponentHandler::BeginEdit&);
    bool log_request(bool is_host_plugin,
                     const YaComponentHandler::PerformEdit&);
    bool log_request(bool is_host_plugin, const YaComponentHandler::EndEdit&);
    bool log_request(bool is_host_plugin,
                     const YaComponentHandler::RestartComponent&);
    bool log_request(bool is_host_plugin, const YaComponentHandler2::SetDirty&);
    bool log_request(bool is_host_plugin,
                     const YaComponentHandler2::RequestOpenEditor&);
    bool log_request(bool is_host_plugin,
                     const YaComponentHandler2::StartGroupEdit&);
    bool log_request(bool is_host_plugin,
                     const YaComponentHandler2::FinishGroupEdit&);
    bool log_request(bool is_host_plugin,
                     const YaComponentHandler3::CreateContextMenu&);
    bool log_request(
        bool is_host_plugin,
        const YaComponentHandlerBusActivation::RequestBusActivation&);
    bool log_request(bool is_host_plugin, const YaContextMenu::AddItem&);
    bool log_request(bool is_host_plugin, const YaContextMenu::RemoveItem&);
    bool log_request(bool is_host_plugin, const YaContextMenu::Popup&);
    bool log_request(bool is_host_plugin, const YaHostApplication::GetName&);
    bool log_request(bool is_host_plugin, const YaPlugFrame::ResizeView&);
    bool log_request(bool is_host_plugin,
                     const YaPlugInterfaceSupport::IsPlugInterfaceSupported&);
    bool log_request(bool is_host_plugin, const YaProgress::Start&);
    bool log_request(bool is_host_plugin, const YaProgress::Update&);
    bool log_request(bool is_host_plugin, const YaProgress::Finish&);
    bool log_request(bool is_host_plugin,
                     const YaUnitHandler::NotifyUnitSelection&);
    bool log_request(bool is_host_plugin,
                     const YaUnitHandler::NotifyProgramListChange&);
    bool log_request(bool is_host_plugin,
                     const YaUnitHandler2::NotifyUnitByBusChange&);

    void log_response(bool is_host_plugin, const Ack&);
    void log_response(bool is_host_plugin,
                      const UniversalTResult&,
                      bool from_cache = false);
    void log_response(bool is_host_plugin,
                      const Vst3PluginFactoryProxy::ConstructArgs&);
    void log_response(
        bool is_host_plugin,
        const std::variant<Vst3PluginProxy::ConstructArgs, UniversalTResult>&);
    void log_response(bool is_host_plugin,
                      const Vst3PluginProxy::InitializeResponse&);
    void log_response(bool is_host_plugin,
                      const Vst3PluginProxy::GetStateResponse&);
    void log_response(bool is_host_plugin,
                      const YaEditController::GetParameterInfoResponse&,
                      bool from_cache = false);
    void log_response(bool is_host_plugin,
                      const YaEditController::GetParamStringByValueResponse&);
    void log_response(bool is_host_plugin,
                      const YaEditController::GetParamValueByStringResponse&);
    void log_response(bool is_host_plugin,
                      const YaEditController::CreateViewResponse&);
    void log_response(bool is_host_plugin,
                      const YaKeyswitchController::GetKeyswitchInfoResponse&);
    void log_response(
        bool is_host_plugin,
        const YaMidiMapping::GetMidiControllerAssignmentResponse&);
    void log_response(
        bool is_host_plugin,
        const YaNoteExpressionController::GetNoteExpressionInfoResponse&);
    void log_response(bool is_host_plugin,
                      const YaNoteExpressionController::
                          GetNoteExpressionStringByValueResponse&);
    void log_response(bool is_host_plugin,
                      const YaNoteExpressionController::
                          GetNoteExpressionValueByStringResponse&);
    void log_response(bool is_host_plugin,
                      const YaNoteExpressionPhysicalUIMapping::
                          GetNotePhysicalUIMappingResponse&);
    void log_response(bool is_host_plugin,
                      const YaParameterFinder::FindParameterResponse&);
    void log_response(
        bool is_host_plugin,
        const YaParameterFunctionName::GetParameterIDFromFunctionNameResponse&);
    void log_response(bool is_host_plugin, const YaPlugView::GetSizeResponse&);
    void log_response(bool is_host_plugin,
                      const YaPlugView::CheckSizeConstraintResponse&);
    void log_response(bool is_host_plugin, const Configuration&);
    void log_response(bool is_host_plugin,
                      const YaProgramListData::GetProgramDataResponse&);
    void log_response(bool is_host_plugin,
                      const YaUnitData::GetUnitDataResponse&);
    void log_response(bool is_host_plugin,
                      const YaUnitInfo::GetUnitInfoResponse&);
    void log_response(bool is_host_plugin,
                      const YaUnitInfo::GetProgramListInfoResponse&);
    void log_response(bool is_host_plugin,
                      const YaUnitInfo::GetProgramNameResponse&);
    void log_response(bool is_host_plugin,
                      const YaUnitInfo::GetProgramInfoResponse&);
    void log_response(bool is_host_plugin,
                      const YaUnitInfo::GetProgramPitchNameResponse&);
    void log_response(bool is_host_plugin,
                      const YaUnitInfo::GetUnitByBusResponse&);
    void log_response(bool is_host_plugin,
                      const YaXmlRepresentationController::
                          GetXmlRepresentationStreamResponse&);

    void log_response(bool is_host_plugin,
                      const YaAudioProcessor::GetBusArrangementResponse&);
    void log_response(bool is_host_plugin,
                      const YaAudioProcessor::ProcessResponse&);
    void log_response(bool is_host_plugin,
                      const YaComponent::GetControllerClassIdResponse&);
    void log_response(bool is_host_plugin,
                      const YaComponent::GetBusInfoResponse&,
                      bool from_cache = false);
    void log_response(bool is_host_plugin,
                      const YaComponent::GetRoutingInfoResponse&);
    void log_response(bool is_host_plugin,
                      const YaComponent::SetActiveResponse&);
    void log_response(
        bool is_host_plugin,
        const YaPrefetchableSupport::GetPrefetchableSupportResponse&);

    void log_response(bool is_host_plugin,
                      const YaComponentHandler3::CreateContextMenuResponse&);
    void log_response(bool is_host_plugin,
                      const YaHostApplication::GetNameResponse&);
    void log_response(bool is_host_plugin, const YaProgress::StartResponse&);

    template <typename T>
    void log_response(bool is_host_plugin,
                      const PrimitiveResponse<T>& value,
                      bool from_cache = false) {
        // For logging all primitive return values other than `tresult`
        log_response_base(is_host_plugin, [&](auto& message) {
            if constexpr (std::is_same_v<T, bool>) {
                message << (value ? "true" : "false");
            } else {
                message << value;
            }

            if (from_cache) {
                message << " (from cache)";
            }
        });
    }

    /**
     * @see Logger::log_trace
     */
    template <invocable_returning<std::string> F>
    inline void log_trace(F&& fn) {
        logger_.log_trace(std::forward<F>(fn));
    }

    Logger& logger_;

   private:
    /**
     * Log a request with a standard prefix based on the boolean flag we pass to
     * every logging function so we don't have to repeat it everywhere.
     *
     * Returns `true` if the log message was displayed, and the response should
     * thus also be logged.
     */
    template <std::invocable<std::ostringstream&> F>
    bool log_request_base(bool is_host_plugin,
                          Logger::Verbosity min_verbosity,
                          F callback) {
        if (logger_.verbosity_ >= min_verbosity) [[unlikely]] {
            std::ostringstream message;
            if (is_host_plugin) {
                message << "[host -> plugin] >> ";
            } else {
                message << "[plugin -> host] >> ";
            }

            callback(message);
            log(message.str());

            return true;
        } else {
            return false;
        }
    }

    template <std::invocable<std::ostringstream&> F>
    bool log_request_base(bool is_host_plugin, F callback) {
        return log_request_base(is_host_plugin, Logger::Verbosity::most_events,
                                callback);
    }

    /**
     * Log a response with a standard prefix based on the boolean flag we pass
     * to every logging function so we don't have to repeat it everywhere.
     *
     * This should only be called when the corresonding `log_request()` returned
     * `true`.
     */
    template <std::invocable<std::ostringstream&> F>
    void log_response_base(bool is_host_plugin, F callback) {
        std::ostringstream message;
        if (is_host_plugin) {
            message << "[plugin <- host]    ";
        } else {
            message << "[host <- plugin]    ";
        }

        callback(message);
        log(message.str());
    }
};
