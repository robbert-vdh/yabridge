// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2023 Robbert van der Helm
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
                             const clap::factory::plugin_factory::List&) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << "clap_plugin_factory::list()";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::factory::plugin_factory::Create& request) {
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
        for (const auto& [supported, extension_name] :
             request.supported_host_extensions.list()) {
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
    const clap::ext::audio_ports_config::plugin::Count& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_audio_ports_config::count()";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::audio_ports_config::plugin::Get& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_audio_ports_config::get(index = "
                << request.index << ", *config)";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::audio_ports_config::plugin::Select& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_audio_ports_config::select(config_id = "
                << request.config_id << ")";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::gui::plugin::IsApiSupported& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_gui::is_api_supported(api = ";
        switch (request.api) {
            case clap::ext::gui::ApiType::X11:
            default:
                message << "\"" << CLAP_WINDOW_API_X11
                        << "\" (will be translated to \""
                        << CLAP_WINDOW_API_WIN32 << "\")";
                break;
        }
        message << ", is_floating = "
                << (request.is_floating ? "true" : "false") << ")";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::gui::plugin::Create& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id << ": clap_plugin_gui::create(api = ";
        switch (request.api) {
            case clap::ext::gui::ApiType::X11:
            default:
                message << "\"" << CLAP_WINDOW_API_X11
                        << "\" (will be translated to \""
                        << CLAP_WINDOW_API_WIN32 << "\")";
                break;
        }
        message << ", is_floating = "
                << (request.is_floating ? "true" : "false") << ")";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::gui::plugin::Destroy& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id << ": clap_plugin_gui::destroy()";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::gui::plugin::SetScale& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_gui::set_scale(scale = " << request.scale
                << ")";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::gui::plugin::GetSize& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_gui::get_size(*width, *height)";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::gui::plugin::CanResize& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id << ": clap_plugin_gui::can_resize()";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::gui::plugin::GetResizeHints& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_gui::get_resize_hints(*hints)";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::gui::plugin::AdjustSize& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_gui::adjust_size(*width = " << request.width
                << ", *height = " << request.height << ")";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::gui::plugin::SetSize& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_gui::set_size(width = " << request.width
                << ", height = " << request.height << ")";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::gui::plugin::SetParent& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_gui::set_parent(window = <X11 window "
                << request.x11_window << ">)";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::gui::plugin::Show& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id << ": clap_plugin_gui::show()";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::gui::plugin::Hide& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id << ": clap_plugin_gui::hide()";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::note_name::plugin::Count& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id << ": clap_plugin_note_name::count()";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::note_name::plugin::Get& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_note_name::get(index = " << request.index
                << ", *note_name)";
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

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::params::plugin::GetInfos& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_params::get_info(..., *param_info) (batched)";
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
                             const clap::ext::latency::plugin::Get& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id << ": clap_plugin_latency::get()";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::render::plugin::HasHardRealtimeRequirement& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_render::has_hard_realtime_requirement()";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::render::plugin::Set& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id << ": clap_plugin_render::set(mode = ";
        switch (request.mode) {
            case CLAP_RENDER_REALTIME:
                message << "CLAP_RENDER_REALTIME";
                break;
            case CLAP_RENDER_OFFLINE:
                message << "CLAP_RENDER_OFFLINE";
                break;
            default:
                message << request.mode << " (unknown)";
                break;
        }
        message << ")";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::state::plugin::Save& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_state::save(clap_ostream_t*)";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::state::plugin::Load& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message
            << request.instance_id
            << ": clap_plugin_state::load(stream = <clap_istream_t* containing "
            << request.stream.size() << " bytes>)";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::voice_info::plugin::Get& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_voice_info::get(*info)";
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

bool ClapLogger::log_request(
    bool is_host_plugin,
    const MessageReference<clap::plugin::Process>& request_wrapper) {
    return log_request_base(
        is_host_plugin, Logger::Verbosity::all_events, [&](auto& message) {
            // This is incredibly verbose, but if you're really a plugin that
            // handles processing in a weird way you're going to need all of
            // this
            const clap::plugin::Process& request = request_wrapper.get();

            // TODO: The channel counts are now capped at what the plugin
            //       supports (based on the audio buffers we set up during
            //       `IAudioProcessor::setActive()`). Some hosts may send more
            //       buffers, but we don't reflect that in the output right now.
            std::ostringstream num_input_channels;
            num_input_channels << "[";
            bool is_first = true;
            for (size_t i = 0; i < request.process.audio_inputs_.size(); i++) {
                const auto& port = request.process.audio_inputs_[i];
                num_input_channels << (is_first ? "" : ", ")
                                   << port.channel_count;
                if (port.latency) {
                    num_input_channels << " (" << port.latency
                                       << " sample latency)";
                }
                if (port.constant_mask > 0) {
                    num_input_channels << " (silence)";
                }

                is_first = false;
            }
            num_input_channels << "]";

            std::ostringstream num_output_channels;
            num_output_channels << "[";
            is_first = true;
            for (size_t i = 0; i < request.process.audio_outputs_.size(); i++) {
                const auto& port = request.process.audio_outputs_[i];
                num_output_channels << (is_first ? "" : ", ")
                                    << port.channel_count;
                if (port.latency) {
                    num_output_channels << " (" << port.latency
                                        << " sample latency)";
                }
                if (port.constant_mask > 0) {
                    num_output_channels << " (silence)";
                }

                is_first = false;
            }
            num_output_channels << "]";

            message << request.instance_id
                    << ": clap_plugin::process(process = <clap_process_t* with "
                       "steady_time = "
                    << request.process.steady_time_
                    << ", frames_count = " << request.process.frames_count_
                    << ", transport = "
                    << (request.process.transport_ ? "<clap_event_transport_t*>"
                                                   : "<nullptr>")
                    << ", audio_input_channels = " << num_input_channels.str()
                    << ", audio_output_channels = " << num_output_channels.str()
                    << ", in_events = <clap_input_events* with "
                    << request.process.in_events_.size()
                    << " events>, out_events = <clap_out_events_t*>>)";
        });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::params::plugin::Flush& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id
                << ": clap_plugin_params::flush(*in = <clap_input_events_t* "
                   "containing "
                << request.in.size() << " events>, *out)";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::tail::plugin::Get& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.instance_id << ": clap_plugin_tail::get()";
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
    const clap::ext::audio_ports_config::host::Rescan& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.owner_instance_id
                << ": clap_host_audio_ports_config::rescan()";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::gui::host::ResizeHintsChanged& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.owner_instance_id
                << ": clap_host_gui::resize_hints_changed()";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::gui::host::RequestResize& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.owner_instance_id
                << ": clap_host_gui::request_resize(width = " << request.width
                << ", height = " << request.height << ")";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::gui::host::RequestShow& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.owner_instance_id
                << ": clap_host_gui::request_show()";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::gui::host::RequestHide& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.owner_instance_id
                << ": clap_host_gui::request_hide()";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::gui::host::Closed& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.owner_instance_id
                << ": clap_host_gui::closed(was_destroyed = "
                << request.was_destroyed << ")";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::note_name::host::Changed& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.owner_instance_id
                << ": clap_host_note_name::changed()";
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

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::latency::host::Changed& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.owner_instance_id
                << ": clap_host_latency::changed()";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::state::host::MarkDirty& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.owner_instance_id
                << ": clap_host_state::mark_dirty()";
    });
}

bool ClapLogger::log_request(
    bool is_host_plugin,
    const clap::ext::voice_info::host::Changed& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.owner_instance_id
                << ": clap_host_voice_info::changed()";
    });
}

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::log::host::Log& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.owner_instance_id
                << ": clap_host_log::log(severity = ";
        switch (request.severity) {
            case CLAP_LOG_DEBUG:
                message << "CLAP_LOG_DEBUG";
                break;
            case CLAP_LOG_INFO:
                message << "CLAP_LOG_INFO";
                break;
            case CLAP_LOG_WARNING:
                message << "CLAP_LOG_WARNING";
                break;
            case CLAP_LOG_ERROR:
                message << "CLAP_LOG_ERROR";
                break;
            case CLAP_LOG_FATAL:
                message << "CLAP_LOG_FATAL";
                break;
            case CLAP_LOG_HOST_MISBEHAVING:
                message << "CLAP_LOG_HOST_MISBEHAVING";
                break;
            case CLAP_LOG_PLUGIN_MISBEHAVING:
                message << "CLAP_LOG_PLUGIN_MISBEHAVING";
                break;
            default:
                message << request.severity << " (unknown)";
                break;
        }
        message << ", message = \"" << request.msg << "\")";
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

bool ClapLogger::log_request(bool is_host_plugin,
                             const clap::ext::tail::host::Changed& request) {
    return log_request_base(is_host_plugin, [&](auto& message) {
        message << request.owner_instance_id << ": clap_host_tail::changed()";
    });
}

void ClapLogger::log_response(bool is_host_plugin, const Ack&) {
    log_response_base(is_host_plugin, [&](auto& message) { message << "ACK"; });
}

void ClapLogger::log_response(
    bool is_host_plugin,
    const clap::factory::plugin_factory::ListResponse& response) {
    log_response_base(is_host_plugin, [&](auto& message) {
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
    const clap::factory::plugin_factory::CreateResponse& response) {
    log_response_base(is_host_plugin, [&](auto& message) {
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
    log_response_base(is_host_plugin, [&](auto& message) {
        message << (response.result ? "true" : "false")
                << ", supported plugin extensions: ";

        bool first = true;
        for (const auto& [supported, extension_name] :
             response.supported_plugin_extensions.list()) {
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
    log_response_base(is_host_plugin, [&](auto& message) {
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
    log_response_base(is_host_plugin, [&](auto& message) {
        if (response.result) {
            message << "true, <clap_audio_port_info_t* for \""
                    << response.result->name
                    << "\", id = " << response.result->id
                    << ", channel_count = " << response.result->channel_count
                    << ">";
        } else {
            message << "false";
        }
    });
}

void ClapLogger::log_response(
    bool is_host_plugin,
    const clap::ext::audio_ports_config::plugin::GetResponse& response) {
    log_response_base(is_host_plugin, [&](auto& message) {
        if (response.result) {
            message << "true, <clap_audio_port_config_t* for \""
                    << response.result->name
                    << "\", id = " << response.result->id << ">";
        } else {
            message << "false";
        }
    });
}

void ClapLogger::log_response(
    bool is_host_plugin,
    const clap::ext::gui::plugin::GetSizeResponse& response) {
    log_response_base(is_host_plugin, [&](auto& message) {
        if (response.result) {
            message << "true, *width = " << response.width
                    << ", *height = " << response.height;
        } else {
            message << "false";
        }
    });
}

void ClapLogger::log_response(
    bool is_host_plugin,
    const clap::ext::gui::plugin::GetResizeHintsResponse& response) {
    log_response_base(is_host_plugin, [&](auto& message) {
        if (response.result) {
            message
                << "true, <clap_resize_hints_t* with can_resize_horizontally = "
                << (response.result->can_resize_horizontally ? "true" : "false")
                << ", can_resize_vertically = "
                << (response.result->can_resize_vertically ? "true" : "false")
                << ", preserve_aspect_ratio = "
                << (response.result->preserve_aspect_ratio ? "true" : "false")
                << ", aspect_ratio_width = "
                << response.result->aspect_ratio_width
                << ", aspect_ratio_height = "
                << response.result->aspect_ratio_height << ">";
        } else {
            message << "false";
        }
    });
}

void ClapLogger::log_response(
    bool is_host_plugin,
    const clap::ext::gui::plugin::AdjustSizeResponse& response) {
    log_response_base(is_host_plugin, [&](auto& message) {
        if (response.result) {
            message << "true, *width = " << response.updated_width
                    << ", *height = " << response.updated_height;
        } else {
            message << "false";
        }
    });
}

void ClapLogger::log_response(
    bool is_host_plugin,
    const clap::ext::note_name::plugin::GetResponse& response) {
    log_response_base(is_host_plugin, [&](auto& message) {
        if (response.result) {
            message << "true, <clap_note_port_info_t* for \""
                    << response.result->name
                    << "\" with port = " << response.result->port
                    << ", key = " << response.result->key
                    << ", channel = " << response.result->channel << ">";
        } else {
            message << "false";
        }
    });
}

void ClapLogger::log_response(
    bool is_host_plugin,
    const clap::ext::note_ports::plugin::GetResponse& response) {
    log_response_base(is_host_plugin, [&](auto& message) {
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
    const clap::ext::params::plugin::GetInfosResponse& response,
    bool from_cache) {
    log_response_base(is_host_plugin, [&](auto& message) {
        message << "<clap_param_info_t*> for " << response.infos.size()
                << " parameters";
        if (from_cache) {
            message << " (from cache)";
        }
    });
}

void ClapLogger::log_response(
    bool is_host_plugin,
    const clap::ext::params::plugin::GetValueResponse& response) {
    log_response_base(is_host_plugin, [&](auto& message) {
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
    log_response_base(is_host_plugin, [&](auto& message) {
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
    log_response_base(is_host_plugin, [&](auto& message) {
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
    log_response_base(is_host_plugin, [&](auto& message) {
        message << "<clap_output_events_t* containing " << response.out.size()
                << " events>";
    });
}

void ClapLogger::log_response(
    bool is_host_plugin,
    const clap::ext::state::plugin::SaveResponse& response) {
    log_response_base(is_host_plugin, [&](auto& message) {
        if (response.result) {
            message << "true, <clap_ostream_t* containing "
                    << response.result->size() << " bytes>";
        } else {
            message << "false";
        }
    });
}

void ClapLogger::log_response(
    bool is_host_plugin,
    const clap::ext::voice_info::plugin::GetResponse& response) {
    log_response_base(is_host_plugin, [&](auto& message) {
        if (response.result) {
            message << "true, <clap_voice_info_t* with voice_count = "
                    << response.result->voice_count
                    << ", voice_capacity = " << response.result->voice_capacity
                    << ", flags = "
                    << std::bitset<sizeof(response.result->flags) * 8>(
                           response.result->flags)
                    << ">";
        } else {
            message << "false";
        }
    });
}

void ClapLogger::log_response(bool is_host_plugin,
                              const clap::plugin::ProcessResponse& response) {
    log_response_base(is_host_plugin, [&](auto& message) {
        // This is incredibly verbose, but if you're really a plugin that
        // handles processing in a weird way you're going to need all of this
        assert(response.output_data.audio_outputs &&
               response.output_data.out_events);

        std::ostringstream num_output_channels;
        num_output_channels << "[";
        bool is_first = true;
        for (size_t i = 0; i < response.output_data.audio_outputs->size();
             i++) {
            const auto& port = (*response.output_data.audio_outputs)[i];
            num_output_channels << (is_first ? "" : ", ") << port.channel_count;
            if (port.latency) {
                num_output_channels << " (" << port.latency
                                    << " sample latency)";
            }
            if (port.constant_mask > 0) {
                num_output_channels << " (silence)";
            }

            is_first = false;
        }
        num_output_channels << "]";

        switch (response.result) {
            case CLAP_PROCESS_ERROR:
                message << "CLAP_PROCESS_ERROR";
                break;
            case CLAP_PROCESS_CONTINUE:
                message << "CLAP_PROCESS_CONTINUE";
                break;
            case CLAP_PROCESS_CONTINUE_IF_NOT_QUIET:
                message << "CLAP_PROCESS_CONTINUE_IF_NOT_QUIET";
                break;
            case CLAP_PROCESS_TAIL:
                message << "CLAP_PROCESS_TAIL";
                break;
            case CLAP_PROCESS_SLEEP:
                message << "CLAP_PROCESS_SLEEP";
                break;
            default:
                message << "unknown status " << response.result;
                break;
        }

        message << ", <clap_audio_buffer_t array with "
                << num_output_channels.str()
                << " channels>, <clap_output_events_t* with "
                << response.output_data.out_events->size() << " events>";
    });
}

void ClapLogger::log_response(bool is_host_plugin, const Configuration&) {
    log_response_base(is_host_plugin,
                      [&](auto& message) { message << "<Configuration>"; });
}
