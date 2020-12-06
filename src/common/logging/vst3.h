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

    // For every object we send using `Vst3MessageHandler` we have overloads
    // that print information about the request and the response. The boolean
    // flag here indicates whether the request was initiated on the host side
    // (what we'll call a control message).

    void log_request(bool is_host_vst, const WantsConfiguration&);
    void log_request(bool is_host_vst, const WantsPluginFactory&);

    void log_response(bool is_host_vst, const Configuration&);
    void log_response(bool is_host_vst, const YaPluginFactory&);

    Logger& logger;

   private:
    /**
     * Get the `host -> vst` or `vst -> host` prefix based on the boolean flag
     * we pass to every logging function so we don't have to repeat it
     * everywhere.
     */
    inline std::string get_log_prefix(bool is_host_vst) {
        return is_host_vst ? "[host -> vst]" : "[vst -> host]";
    }
};
