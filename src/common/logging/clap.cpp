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

#include "clap.h"

#include <bitset>

#include "../serialization/clap.h"

ClapLogger::ClapLogger(Logger& generic_logger) : logger_(generic_logger) {}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::plugin_factory::List&) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << "clap_plugin_factory::list()";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::plugin_factory::Create& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << "clap_plugin_factory::create(host = <clap_host_t*>, "
                   "plugin_id = \""
                << request.plugin_id << "\")";
    });
}

bool ClapLogger::log_request(bool is_host_plugin, const WantsConfiguration&) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << "Requesting <Configuration>";
    });
}

// void ClapLogger::log_response(bool is_host_plugin, const Ack&) {
//     log_response_base(is_host_plugin, [&](auto& message) { message << "ACK";
//     });
// }

void ClapLogger::log_response(
    bool is_host_plugin,
    const clap::plugin_factory::ListResponse& response) {
    return log_response_base(is_host_plugin, [&](auto& message) {
        if (response.descriptors) {
            message << "<clap_plugin_factory containing "
                    << response.descriptors->size() << " plugin descriptors>";
        } else {
            message << "<not supported>";
        }
    });
}

void ClapLogger::log_response(
    bool is_host_plugin,
    const clap::plugin_factory::CreateResponse& response) {
    return log_response_base(is_host_plugin, [&](auto& message) {
        if (response.instance_id) {
            message << "<clap_plugin_t* with instance ID "
                    << *response.instance_id << ">";
        } else {
            message << "<nullptr*>";
        }
    });
}

void ClapLogger::log_response(bool is_host_plugin, const Configuration&) {
    log_response_base(is_host_plugin,
                      [&](auto& message) { message << "<Configuration>"; });
}
