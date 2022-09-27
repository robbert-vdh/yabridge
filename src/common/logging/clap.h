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

    /**
     * Log calls to `clap_plugin::get_extension()` and
     * `clap_host::get_extension()`. This makes it possible to tell which
     * extensions the host or plugin is querying, and which of those we don't
     * support yet.
     *
     * @param where The name of the function where this query occurred. In the
     *   format `clap_foo::get_extension`, without parentheses. This is a `const
     *   char*` to avoid allocations.
     * @param result True if we returned an extension pointer, or false if we
     *   returned a null pointer.
     * @param id The ID of the extension the plugin or host was trying to query.
     */
    void log_extension_query(const char* where,
                             bool result,
                             const char* extension_id);

    /**
     * Logging for `clap_host::request_callback()`. This is handled purely on
     * the Wine plugin host side.
     */
    void log_callback_request(size_t instance_id);
    /**
     * Logging for `clap_plugin::on_main_thread()`. This is handled purely on
     * the Wine plugin host side.
     */
    void log_on_main_thread(size_t instance_id);

    // For every object we send using `ClapMessageHandler` we have overloads
    // that print information about the request and the response. The boolean
    // flag here indicates whether the request was initiated on the host side
    // (what we'll call a control message).
    // `log_response()` should only be called if the corresponding
    // `log_request()` call returned `true`. This way we can filter out the
    // log message for the response together with the request.

    // Main thread control messages
    bool log_request(bool is_host_plugin, const clap::plugin_factory::List&);
    bool log_request(bool is_host_plugin, const clap::plugin_factory::Create&);
    bool log_request(bool is_host_plugin, const clap::plugin::Init&);
    bool log_request(bool is_host_plugin, const clap::plugin::Destroy&);
    bool log_request(bool is_host_plugin, const clap::plugin::Activate&);
    bool log_request(bool is_host_plugin, const clap::plugin::Deactivate&);
    bool log_request(bool is_host_plugin,
                     const clap::ext::audio_ports::plugin::Count&);
    bool log_request(bool is_host_plugin,
                     const clap::ext::audio_ports::plugin::Get&);
    bool log_request(bool is_host_plugin,
                     const clap::ext::note_ports::plugin::Count&);
    bool log_request(bool is_host_plugin,
                     const clap::ext::note_ports::plugin::Get&);
    bool log_request(bool is_host_plugin,
                     const clap::ext::params::plugin::Count&);
    bool log_request(bool is_host_plugin,
                     const clap::ext::params::plugin::GetInfo&);
    bool log_request(bool is_host_plugin,
                     const clap::ext::params::plugin::GetValue&);
    bool log_request(bool is_host_plugin,
                     const clap::ext::params::plugin::ValueToText&);
    bool log_request(bool is_host_plugin,
                     const clap::ext::params::plugin::TextToValue&);
    bool log_request(bool is_host_plugin,
                     const clap::ext::latency::plugin::Get&);
    bool log_request(bool is_host_plugin,
                     const clap::ext::state::plugin::Save&);
    bool log_request(bool is_host_plugin,
                     const clap::ext::state::plugin::Load&);

    // Audio thread control messages
    bool log_request(bool is_host_plugin, const clap::plugin::StartProcessing&);
    bool log_request(bool is_host_plugin, const clap::plugin::StopProcessing&);
    bool log_request(bool is_host_plugin, const clap::plugin::Reset&);
    bool log_request(bool is_host_plugin,
                     const clap::ext::params::plugin::Flush&);
    bool log_request(bool is_host_plugin, const clap::ext::tail::plugin::Get&);

    // Main thread callbacks
    bool log_request(bool is_host_plugin, const WantsConfiguration&);
    bool log_request(bool is_host_plugin, const clap::host::RequestRestart&);
    bool log_request(bool is_host_plugin, const clap::host::RequestProcess&);
    bool log_request(
        bool is_host_plugin,
        const clap::ext::audio_ports::host::IsRescanFlagSupported&);
    bool log_request(bool is_host_plugin,
                     const clap::ext::audio_ports::host::Rescan&);
    bool log_request(bool is_host_plugin,
                     const clap::ext::note_ports::host::SupportedDialects&);
    bool log_request(bool is_host_plugin,
                     const clap::ext::note_ports::host::Rescan&);
    bool log_request(bool is_host_plugin,
                     const clap::ext::params::host::Rescan&);
    bool log_request(bool is_host_plugin,
                     const clap::ext::params::host::Clear&);
    bool log_request(bool is_host_plugin,
                     const clap::ext::params::host::RequestFlush&);
    bool log_request(bool is_host_plugin,
                     const clap::ext::latency::host::Changed&);
    bool log_request(bool is_host_plugin,
                     const clap::ext::state::host::MarkDirty&);

    // Audio thread callbacks
    bool log_request(bool is_host_plugin,
                     const clap::ext::tail::host::Changed&);

    // Main thread control message responses
    void log_response(bool is_host_plugin, const Ack&);
    void log_response(bool is_host_plugin,
                      const clap::plugin_factory::ListResponse&);
    void log_response(bool is_host_plugin,
                      const clap::plugin_factory::CreateResponse&);
    void log_response(bool is_host_plugin, const clap::plugin::InitResponse&);
    void log_response(bool is_host_plugin,
                      const clap::plugin::ActivateResponse&);
    void log_response(bool is_host_plugin,
                      const clap::ext::audio_ports::plugin::GetResponse&);
    void log_response(bool is_host_plugin,
                      const clap::ext::note_ports::plugin::GetResponse&);
    void log_response(bool is_host_plugin,
                      const clap::ext::params::plugin::GetInfoResponse&);
    void log_response(bool is_host_plugin,
                      const clap::ext::params::plugin::GetValueResponse&);
    void log_response(bool is_host_plugin,
                      const clap::ext::params::plugin::ValueToTextResponse&);
    void log_response(bool is_host_plugin,
                      const clap::ext::params::plugin::TextToValueResponse&);
    void log_response(bool is_host_plugin,
                      const clap::ext::params::plugin::FlushResponse&);
    void log_response(bool is_host_plugin,
                      const clap::ext::state::plugin::SaveResponse&);

    // Main thread callback responses
    void log_response(bool is_host_plugin, const Configuration&);

    template <typename T>
    void log_response(bool is_host_plugin,
                      const PrimitiveResponse<T>& value,
                      bool from_cache = false) {
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
