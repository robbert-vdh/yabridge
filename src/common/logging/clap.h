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

#include "../serialization/clap.h"
#include "common.h"

/**
 * Wraps around `Logger` to provide CLAP-specific logging functionality for
 * debugging plugins. This way we can have all the complex initialisation be
 * performed in one place.
 */
class ClapLogger {
   public:
    ClapLogger(Logger& generic_logger);

    /**
     * @see Logger::log
     */
    inline void log(const std::string& message) { logger_.log(message); }

    // TODO: Logging for extension queries, factory type queries

    // For every object we send using `ClapMessageHandler` we have overloads
    // that print information about the request and the response. The boolean
    // flag here indicates whether the request was initiated on the host side
    // (what we'll call a control message).
    // `log_response()` should only be called if the corresponding
    // `log_request()` call returned `true`. This way we can filter out the
    // log message for the response together with the request.

    bool log_request(bool is_host_plugin, const clap::plugin_factory::List&);
    bool log_request(bool is_host_plugin, const clap::plugin_factory::Create&);
    bool log_request(bool is_host_plugin, const clap::plugin::Destroy&);

    // TODO: Audio thread requests
    // bool log_request(bool is_host_plugin,
    //                  const YaAudioProcessor::SetBusArrangements&);

    bool log_request(bool is_host_plugin, const WantsConfiguration&);

    void log_response(bool is_host_plugin, const Ack&);
    void log_response(bool is_host_plugin,
                      const clap::plugin_factory::ListResponse&);
    void log_response(bool is_host_plugin,
                      const clap::plugin_factory::CreateResponse&);

    // TODO: Audio thread responses
    // void log_response(bool is_host_plugin,
    //                   const YaAudioProcessor::GetBusArrangementResponse&);

    void log_response(bool is_host_plugin, const Configuration&);

    // TODO: Universal response
    // template <typename T>
    // void log_response(bool is_host_plugin,
    //                   const PrimitiveWrapper<T>& value,
    //                   bool from_cache = false) {
    //     // For logging all primitive return values other than `tresult`
    //     log_response_base(is_host_plugin, [&](auto& message) {
    //         message << value;
    //         if (from_cache) {
    //             message << " (from cache)";
    //         }
    //     });
    // }

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
                message << "[host -> vst] >> ";
            } else {
                message << "[vst -> host] >> ";
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
