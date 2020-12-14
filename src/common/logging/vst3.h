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

    void log_request(bool is_host_vst, const YaComponent::Construct&);
    void log_request(bool is_host_vst, const YaComponent::Destruct&);
    void log_request(bool is_host_vst, const YaComponent::Initialize&);
    void log_request(bool is_host_vst, const YaComponent::Terminate&);
    void log_request(bool is_host_vst, const YaComponent::SetIoMode&);
    void log_request(bool is_host_vst, const YaComponent::GetBusCount&);
    void log_request(bool is_host_vst, const YaComponent::GetBusInfo&);
    void log_request(bool is_host_vst, const YaComponent::GetRoutingInfo&);
    void log_request(bool is_host_vst, const YaComponent::ActivateBus&);
    void log_request(bool is_host_vst, const YaComponent::SetActive&);
    void log_request(bool is_host_vst, const YaComponent::SetState&);
    void log_request(bool is_host_vst, const YaComponent::GetState&);
    void log_request(bool is_host_vst, const YaComponent::SetBusArrangements&);
    void log_request(bool is_host_vst, const YaComponent::GetBusArrangement&);
    void log_request(bool is_host_vst,
                     const YaComponent::CanProcessSampleSize&);
    void log_request(bool is_host_vst, const YaComponent::GetLatencySamples&);
    void log_request(bool is_host_vst, const YaComponent::SetupProcessing&);
    void log_request(bool is_host_vst, const YaPluginFactory::Construct&);
    void log_request(bool is_host_vst, const YaPluginFactory::SetHostContext&);
    void log_request(bool is_host_vst, const WantsConfiguration&);

    void log_response(bool is_host_vst, const Ack&);
    void log_response(
        bool is_host_vst,
        const std::variant<YaComponent::ConstructArgs, UniversalTResult>&);
    void log_response(bool is_host_vst, const YaComponent::GetBusInfoResponse&);
    void log_response(bool is_host_vst,
                      const YaComponent::GetRoutingInfoResponse&);
    void log_response(bool is_host_vst, const YaComponent::GetStateResponse&);
    void log_response(bool is_host_vst,
                      const YaComponent::GetBusArrangementResponse&);
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
    void log_request_base(bool is_host_vst, F callback) {
        if (BOOST_UNLIKELY(logger.verbosity >=
                           Logger::Verbosity::most_events)) {
            std::ostringstream message;
            if (is_host_vst) {
                message << "[host -> vst] >> ";
            } else {
                message << "[vst -> host] >> ";
            }

            callback(message);
            log(message.str());
        }
    }

    /**
     * Log a response with a standard prefix based on the boolean flag we pass
     * to every logging function so we don't have to repeat it everywhere.
     */
    template <std::invocable<std::ostringstream&> F>
    void log_response_base(bool is_host_vst, F callback) {
        if (BOOST_UNLIKELY(logger.verbosity >=
                           Logger::Verbosity::most_events)) {
            std::ostringstream message;
            if (is_host_vst) {
                message << "[host -> vst]    ";
            } else {
                message << "[vst -> host]    ";
            }

            callback(message);
            log(message.str());
        }
    }
};
