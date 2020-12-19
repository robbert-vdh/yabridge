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

#pragma once

#include <sstream>

#include "../serialization/vst3.h"
#include "common.h"

/**
 * Wraps around `Logger` to provide VST3 specific logging functionality for
 * debugging plugins. This way we can have all the complex initialisation be
 * performed in one place.
 */
class Vst3Logger {
   public:
    Vst3Logger(Logger& generic_logger);

    /**
     * @see Logger::log
     */
    inline void log(const std::string& message) { logger.log(message); }

    /**
     * @see Logger::log_trace
     */
    inline void log_trace(const std::string& message) {
        logger.log_trace(message);
    }

    /**
     * Log about encountering an unknown interface. The location and the UID
     * will be printed when the verbosity level is set to `most_events` or
     * higher. In case we could not get a FUID (because of null pointers, for
     * instance), `std::nullopt` should be passed.
     */
    void log_unknown_interface(const std::string& where,
                               const std::optional<Steinberg::FUID>& uid);

    // For every object we send using `Vst3MessageHandler` we have overloads
    // that print information about the request and the response. The boolean
    // flag here indicates whether the request was initiated on the host side
    // (what we'll call a control message).
    // `log_response()` should only be called if the corresponding
    // `log_request()` call returned `true`. This way we can filter out the
    // log message for the response together with the request.

    bool log_request(bool is_host_vst, const Vst3PlugViewProxy::Destruct&);
    bool log_request(bool is_host_vst, const Vst3PluginProxy::Construct&);
    bool log_request(bool is_host_vst, const Vst3PluginProxy::Destruct&);
    bool log_request(bool is_host_vst, const Vst3PluginProxy::SetState&);
    bool log_request(bool is_host_vst, const Vst3PluginProxy::GetState&);
    bool log_request(bool is_host_vst,
                     const YaAudioProcessor::SetBusArrangements&);
    bool log_request(bool is_host_vst,
                     const YaAudioProcessor::GetBusArrangement&);
    bool log_request(bool is_host_vst,
                     const YaAudioProcessor::CanProcessSampleSize&);
    bool log_request(bool is_host_vst,
                     const YaAudioProcessor::GetLatencySamples&);
    bool log_request(bool is_host_vst,
                     const YaAudioProcessor::SetupProcessing&);
    bool log_request(bool is_host_vst, const YaAudioProcessor::SetProcessing&);
    bool log_request(bool is_host_vst, const YaAudioProcessor::Process&);
    bool log_request(bool is_host_vst, const YaAudioProcessor::GetTailSamples&);
    bool log_request(bool is_host_vst,
                     const YaComponent::GetControllerClassId&);
    bool log_request(bool is_host_vst, const YaComponent::SetIoMode&);
    bool log_request(bool is_host_vst, const YaComponent::GetBusCount&);
    bool log_request(bool is_host_vst, const YaComponent::GetBusInfo&);
    bool log_request(bool is_host_vst, const YaComponent::GetRoutingInfo&);
    bool log_request(bool is_host_vst, const YaComponent::ActivateBus&);
    bool log_request(bool is_host_vst, const YaComponent::SetActive&);
    bool log_request(bool is_host_vst, const YaConnectionPoint::Connect&);
    bool log_request(bool is_host_vst, const YaConnectionPoint::Disconnect&);
    bool log_request(bool is_host_vst,
                     const YaEditController::SetComponentState&);
    bool log_request(bool is_host_vst,
                     const YaEditController::GetParameterCount&);
    bool log_request(bool is_host_vst,
                     const YaEditController::GetParameterInfo&);
    bool log_request(bool is_host_vst,
                     const YaEditController::GetParamStringByValue&);
    bool log_request(bool is_host_vst,
                     const YaEditController::GetParamValueByString&);
    bool log_request(bool is_host_vst,
                     const YaEditController::NormalizedParamToPlain&);
    bool log_request(bool is_host_vst,
                     const YaEditController::PlainParamToNormalized&);
    bool log_request(bool is_host_vst,
                     const YaEditController::GetParamNormalized&);
    bool log_request(bool is_host_vst,
                     const YaEditController::SetParamNormalized&);
    bool log_request(bool is_host_vst,
                     const YaEditController::SetComponentHandler&);
    bool log_request(bool is_host_vst, const YaEditController::CreateView&);
    bool log_request(bool is_host_vst,
                     const YaPlugView::IsPlatformTypeSupported&);
    bool log_request(bool is_host_vst, const YaPlugView::Attached&);
    bool log_request(bool is_host_vst, const YaPlugView::GetSize&);
    bool log_request(bool is_host_vst, const YaPluginBase::Initialize&);
    bool log_request(bool is_host_vst, const YaPluginBase::Terminate&);
    bool log_request(bool is_host_vst, const YaPluginFactory::Construct&);
    bool log_request(bool is_host_vst, const YaPluginFactory::SetHostContext&);

    bool log_request(bool is_host_vst, const WantsConfiguration&);
    bool log_request(bool is_host_vst, const YaComponentHandler::BeginEdit&);
    bool log_request(bool is_host_vst, const YaComponentHandler::PerformEdit&);
    bool log_request(bool is_host_vst, const YaComponentHandler::EndEdit&);
    bool log_request(bool is_host_vst,
                     const YaComponentHandler::RestartComponent&);
    bool log_request(bool is_host_vst, const YaHostApplication::GetName&);

    void log_response(bool is_host_vst, const Ack&);
    void log_response(
        bool is_host_vst,
        const std::variant<Vst3PluginProxy::ConstructArgs, UniversalTResult>&);
    void log_response(bool is_host_vst,
                      const Vst3PluginProxy::GetStateResponse&);
    void log_response(bool is_host_vst,
                      const YaAudioProcessor::GetBusArrangementResponse&);
    void log_response(bool is_host_vst,
                      const YaAudioProcessor::ProcessResponse&);
    void log_response(bool is_host_vst,
                      const YaComponent::GetControllerClassIdResponse&);
    void log_response(bool is_host_vst, const YaComponent::GetBusInfoResponse&);
    void log_response(bool is_host_vst,
                      const YaComponent::GetRoutingInfoResponse&);
    void log_response(bool is_host_vst,
                      const YaEditController::GetParameterInfoResponse&);
    void log_response(bool is_host_vst,
                      const YaEditController::GetParamStringByValueResponse&);
    void log_response(bool is_host_vst,
                      const YaEditController::GetParamValueByStringResponse&);
    void log_response(bool is_host_vst,
                      const YaEditController::CreateViewResponse&);
    void log_response(bool is_host_vst, const YaPlugView::GetSizeResponse&);
    void log_response(bool is_host_vst, const YaPluginFactory::ConstructArgs&);
    void log_response(bool is_host_vst, const Configuration&);

    void log_response(bool is_host_vst,
                      const YaHostApplication::GetNameResponse&);

    template <typename T>
    void log_response(bool is_host_vst, const PrimitiveWrapper<T>& value) {
        // For logging all primitive return values other than `tresult`
        log_response_base(is_host_vst,
                          [&](auto& message) { message << value; });
    }

    Logger& logger;

   private:
    /**
     * Log a request with a standard prefix based on the boolean flag we pass to
     * every logging function so we don't have to repeat it everywhere.
     *
     * Returns `true` if the log message was displayed, and the response should
     * thus also be logged.
     */
    template <std::invocable<std::ostringstream&> F>
    bool log_request_base(bool is_host_vst,
                          Logger::Verbosity min_verbosity,
                          F callback) {
        if (BOOST_UNLIKELY(logger.verbosity >= min_verbosity)) {
            std::ostringstream message;
            if (is_host_vst) {
                message << "[host -> vst] >> ";
            } else {
                message << "[host <- vst] >> ";
            }

            callback(message);
            log(message.str());

            return true;
        } else {
            return false;
        }
    }

    template <std::invocable<std::ostringstream&> F>
    bool log_request_base(bool is_host_vst, F callback) {
        return log_request_base(is_host_vst, Logger::Verbosity::most_events,
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
    void log_response_base(bool is_host_vst, F callback) {
        std::ostringstream message;
        if (is_host_vst) {
            message << "[host -> vst]    ";
        } else {
            message << "[host <- vst]    ";
        }

        callback(message);
        log(message.str());
    }
};
