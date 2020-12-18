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

    void log_request(bool is_host_vst, const Vst3PluginProxy::Construct&);
    void log_request(bool is_host_vst, const Vst3PluginProxy::Destruct&);
    void log_request(bool is_host_vst, const Vst3PluginProxy::SetState&);
    void log_request(bool is_host_vst, const Vst3PluginProxy::GetState&);
    void log_request(bool is_host_vst,
                     const YaAudioProcessor::SetBusArrangements&);
    void log_request(bool is_host_vst,
                     const YaAudioProcessor::GetBusArrangement&);
    void log_request(bool is_host_vst,
                     const YaAudioProcessor::CanProcessSampleSize&);
    void log_request(bool is_host_vst,
                     const YaAudioProcessor::GetLatencySamples&);
    void log_request(bool is_host_vst,
                     const YaAudioProcessor::SetupProcessing&);
    void log_request(bool is_host_vst, const YaAudioProcessor::SetProcessing&);
    void log_request(bool is_host_vst, const YaAudioProcessor::Process&);
    void log_request(bool is_host_vst, const YaAudioProcessor::GetTailSamples&);
    void log_request(bool is_host_vst, const YaComponent::SetIoMode&);
    void log_request(bool is_host_vst, const YaComponent::GetBusCount&);
    void log_request(bool is_host_vst, const YaComponent::GetBusInfo&);
    void log_request(bool is_host_vst, const YaComponent::GetRoutingInfo&);
    void log_request(bool is_host_vst, const YaComponent::ActivateBus&);
    void log_request(bool is_host_vst, const YaComponent::SetActive&);
    void log_request(bool is_host_vst, const YaConnectionPoint::Connect&);
    void log_request(bool is_host_vst,
                     const YaEditController2::SetComponentState&);
    void log_request(bool is_host_vst,
                     const YaEditController2::GetParameterCount&);
    void log_request(bool is_host_vst,
                     const YaEditController2::GetParameterInfo&);
    void log_request(bool is_host_vst,
                     const YaEditController2::GetParamStringByValue&);
    void log_request(bool is_host_vst,
                     const YaEditController2::GetParamValueByString&);
    void log_request(bool is_host_vst,
                     const YaEditController2::NormalizedParamToPlain&);
    void log_request(bool is_host_vst,
                     const YaEditController2::PlainParamToNormalized&);
    void log_request(bool is_host_vst,
                     const YaEditController2::GetParamNormalized&);
    void log_request(bool is_host_vst,
                     const YaEditController2::SetParamNormalized&);
    void log_request(bool is_host_vst, const YaPluginBase::Initialize&);
    void log_request(bool is_host_vst, const YaPluginBase::Terminate&);
    void log_request(bool is_host_vst, const YaPluginFactory::Construct&);
    void log_request(bool is_host_vst, const YaPluginFactory::SetHostContext&);
    void log_request(bool is_host_vst, const WantsConfiguration&);

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
    void log_response(bool is_host_vst, const YaComponent::GetBusInfoResponse&);
    void log_response(bool is_host_vst,
                      const YaComponent::GetRoutingInfoResponse&);
    void log_response(bool is_host_vst,
                      const YaEditController2::GetParameterInfoResponse&);
    void log_response(bool is_host_vst,
                      const YaEditController2::GetParamStringByValueResponse&);
    void log_response(bool is_host_vst,
                      const YaEditController2::GetParamValueByStringResponse&);
    void log_response(bool is_host_vst, const YaPluginFactory::ConstructArgs&);
    void log_response(bool is_host_vst, const Configuration&);

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
     */
    template <std::invocable<std::ostringstream&> F>
    void log_request_base(bool is_host_vst,
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
        }
    }

    template <std::invocable<std::ostringstream&> F>
    void log_request_base(bool is_host_vst, F callback) {
        log_request_base(is_host_vst, Logger::Verbosity::most_events, callback);
    }

    /**
     * Log a response with a standard prefix based on the boolean flag we pass
     * to every logging function so we don't have to repeat it everywhere.
     */
    template <std::invocable<std::ostringstream&> F>
    void log_response_base(bool is_host_vst,
                           Logger::Verbosity min_verbosity,
                           F callback) {
        if (BOOST_UNLIKELY(logger.verbosity >= min_verbosity)) {
            std::ostringstream message;
            if (is_host_vst) {
                message << "[host -> vst]    ";
            } else {
                message << "[host <- vst]    ";
            }

            callback(message);
            log(message.str());
        }
    }

    template <std::invocable<std::ostringstream&> F>
    void log_response_base(bool is_host_vst, F callback) {
        log_response_base(is_host_vst, Logger::Verbosity::most_events,
                          callback);
    }
};
