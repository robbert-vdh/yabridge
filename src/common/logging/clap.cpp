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

#include <clap/ext/audio-ports.h>

#include "../serialization/clap.h"

ClapLogger::ClapLogger(Logger& generic_logger) : logger_(generic_logger) {}

void ClapLogger::log_extension_query(const char* where,
                                     bool result,
                                     const char* extension_id) {
    if (logger_.verbosity_ >= Logger::Verbosity::all_events) [[unlikely]] {
        assert(where && extension_id);

        std::ostringstream message;
        if (result) {
            message << "[extension query] " << where << "(extension_id = \""
                    << extension_id << "\")";
        } else {
            // TODO: DIfferentiate between extensions we don't implement and
            //       extensions the object doesn't implement
            message << "[unknown extension] " << where << "(extension_id = \""
                    << extension_id << "\")";
        }

        log(message.str());
    }
}

void ClapLogger::log_callback_request(size_t instance_id) {
    log_request_base(false, Logger::Verbosity::all_events, [&](auto& message) {
        message << instance_id << ": clap_host::request_callback()";
    });
}

void ClapLogger::log_on_main_thread(size_t instance_id) {
    log_request_base(true, Logger::Verbosity::all_events, [&](auto& message) {
        message << instance_id << ": clap_plugin::on_main_thread()";
    });
}

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

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::plugin::Init& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin::init(), supported host extensions: ";

        bool first = true;
        const auto& supported_extensions = request.supported_host_extensions;
        for (const auto& [supported, extension_name] :
             {std::pair(supported_extensions.supports_audio_ports,
                        CLAP_EXT_AUDIO_PORTS),
              std::pair(supported_extensions.supports_note_ports,
                        CLAP_EXT_NOTE_PORTS),
              std::pair(supported_extensions.supports_params,
                        CLAP_EXT_PARAMS)}) {
            if (!supported) {
                continue;
            }

            if (first) {
                message << '"' << extension_name << '"';
            } else {
                message << ", \"" << extension_name << '"';
            }

            first = false;
        }

        if (first) {
            message << "<none>";
        }
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::plugin::Destroy& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id << ": clap_plugin::destroy()";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::plugin::Activate& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin::activate(sample_rate = "
                << request.sample_rate
                << ", min_frames_count = " << request.min_frames_count
                << ", max_frames_count = " << request.max_frames_count << ")";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::plugin::Deactivate& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id << ": clap_plugin::deactivate()";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::audio_ports::plugin::Count& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_audio_ports::count(is_input = "
                << (request.is_input ? "true" : "false") << ")";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::audio_ports::plugin::Get& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_audio_ports::get(index = " << request.index
                << "is_input = " << (request.is_input ? "true" : "false")
                << ", *info)";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::note_ports::plugin::Count& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_note_ports::count(is_input = "
                << (request.is_input ? "true" : "false") << ")";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::note_ports::plugin::Get& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_note_ports::get(index = " << request.index
                << "is_input = " << (request.is_input ? "true" : "false")
                << ", *info)";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::params::plugin::Count& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id << ": clap_plugin_params::count()";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::params::plugin::GetInfo& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_params::get_info(param_index = "
                << request.param_index << ", *param_info)";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::params::plugin::GetValue& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_params::get_value(param_id = "
                << request.param_id << ", *value)";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::params::plugin::ValueToText& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_params::value_to_text(param_id = "
                << request.param_id << ", value = " << request.value
                << ", *display, size)";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::params::plugin::TextToValue& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_params::text_to_value(param_id = "
                << request.param_id << ", display = \"" << request.display
                << "\", *value)";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::params::plugin::Flush& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        // TODO: Add event counts
        message << request.instance_id
                << ": clap_plugin_params::flush(*in, *out)";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::plugin::StartProcessing& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id << ": clap_plugin::start_processing()";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::plugin::StopProcessing& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id << ": clap_plugin::stop_processing()";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::plugin::Reset& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id << ": clap_plugin::reset()";
    });
}

bool ClapLogger::log_request(bool is_host_plugin, const WantsConfiguration&) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << "Requesting <Configuration>";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::host::RequestRestart& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.owner_instance_id
                << ": clap_host::request_restart()";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::host::RequestProcess& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.owner_instance_id
                << ": clap_host::request_process()";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::audio_ports::host::IsRescanFlagSupported& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.owner_instance_id
                << ": clap_host_audio_ports::is_rescan_flag_supported(flag = "
                << std::bitset<sizeof(request.flag) * 8>(request.flag) << ")";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::audio_ports::host::Rescan& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.owner_instance_id
                << ": clap_host_audio_ports::rescan(flag = "
                << std::bitset<sizeof(request.flags) * 8>(request.flags) << ")";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::note_ports::host::SupportedDialects& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.owner_instance_id
                << ": clap_host_note_ports::supported_dialects()";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::note_ports::host::Rescan& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.owner_instance_id
                << ": clap_host_note_ports::rescan(flag = "
                << std::bitset<sizeof(request.flags) * 8>(request.flags) << ")";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::params::host::Rescan& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.owner_instance_id
                << ": clap_host_params::rescan(flags = "
                << std::bitset<sizeof(request.flags) * 8>(request.flags) << ")";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::params::host::Clear& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.owner_instance_id
                << ": clap_host_params::clear(param_id = " << request.param_id
                << ", flags = "
                << std::bitset<sizeof(request.flags) * 8>(request.flags) << ")";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::params::host::RequestFlush& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.owner_instance_id
                << ": clap_host_params::request_flush()";
    });
}

void ClapLogger::log_response(bool is_host_plugin, const Ack&) {
    log_response_base(is_host_plugin, [&](auto& message) { message << "ACK"; });
}

void ClapLogger::log_response(
    bool is_host_plugin,
    const clap::plugin_factory::ListResponse& response) {
    return log_response_base(is_host_plugin, [&](auto& message) {
        if (response.descriptors) {
            message << "<clap_plugin_factory* containing "
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

void ClapLogger::log_response(bool is_host_plugin,
                              const clap::plugin::InitResponse& response) {
    return log_response_base(is_host_plugin, [&](auto& message) {
        message << (response.result ? "true" : "false")
                << ", supported plugin extensions: ";

        bool first = true;
        const auto& supported_extensions = response.supported_plugin_extensions;
        for (const auto& [supported, extension_name] :
             {std::pair(supported_extensions.supports_audio_ports,
                        CLAP_EXT_AUDIO_PORTS),
              std::pair(supported_extensions.supports_note_ports,
                        CLAP_EXT_NOTE_PORTS),
              std::pair(supported_extensions.supports_params,
                        CLAP_EXT_PARAMS)}) {
            if (!supported) {
                continue;
            }

            if (first) {
                message << '"' << extension_name << '"';
            } else {
                message << ", \"" << extension_name << '"';
            }

            first = false;
        }

        if (first) {
            message << "<none>";
        }
    });
}

void ClapLogger::log_response(bool is_host_plugin,
                              const clap::plugin::ActivateResponse& response) {
    return log_response_base(is_host_plugin, [&](auto& message) {
        message << (response.result ? "true" : "false");
        if (response.result && response.updated_audio_buffers_config) {
            message << ", <new shared memory configuration for \""
                    << response.updated_audio_buffers_config->name << "\", "
                    << response.updated_audio_buffers_config->size << " bytes>";
        }
    });
}

void ClapLogger::log_response(
    bool is_host_plugin,
    const clap::ext::audio_ports::plugin::GetResponse& response) {
    return log_response_base(is_host_plugin, [&](auto& message) {
        if (response.result) {
            message << "true, <clap_audio_port_info_t* for \""
                    << response.result->name << "\">";
        } else {
            message << "false";
        }
    });
}

void ClapLogger::log_response(
    bool is_host_plugin,
    const clap::ext::note_ports::plugin::GetResponse& response) {
    return log_response_base(is_host_plugin, [&](auto& message) {
        if (response.result) {
            message << "true, <clap_note_port_info_t* for \""
                    << response.result->name << "\">";
        } else {
            message << "false";
        }
    });
}

void ClapLogger::log_response(
    bool is_host_plugin,
    const clap::ext::params::plugin::GetInfoResponse& response) {
    return log_response_base(is_host_plugin, [&](auto& message) {
        if (response.result) {
            message << "true, <clap_param_info_t* for \""
                    << response.result->name << "\">";
        } else {
            message << "false";
        }
    });
}

void ClapLogger::log_response(
    bool is_host_plugin,
    const clap::ext::params::plugin::GetValueResponse& response) {
    return log_response_base(is_host_plugin, [&](auto& message) {
        if (response.result) {
            message << "true, " << *response.result;
        } else {
            message << "false";
        }
    });
}

void ClapLogger::log_response(
    bool is_host_plugin,
    const clap::ext::params::plugin::ValueToTextResponse& response) {
    return log_response_base(is_host_plugin, [&](auto& message) {
        if (response.result) {
            message << "true, \"" << *response.result << '"';
        } else {
            message << "false";
        }
    });
}

void ClapLogger::log_response(
    bool is_host_plugin,
    const clap::ext::params::plugin::TextToValueResponse& response) {
    return log_response_base(is_host_plugin, [&](auto& message) {
        if (response.result) {
            message << "true, " << *response.result;
        } else {
            message << "false";
        }
    });
}

void ClapLogger::log_response(
    bool is_host_plugin,
    const clap::ext::params::plugin::FlushResponse& response) {
    return log_response_base(is_host_plugin, [&](auto& message) {
        // TODO: Log output event count
        message << "TODO: Log output event count";
    });
}

void ClapLogger::log_response(bool is_host_plugin, const Configuration&) {
    log_response_base(is_host_plugin,
                      [&](auto& message) { message << "<Configuration>"; });
}
