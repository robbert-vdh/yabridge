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

#include "communication.h"

#include <random>

#include "utils.h"

namespace fs = boost::filesystem;

/**
 * Used for generating random identifiers.
 */
constexpr char alphanumeric_characters[] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

Sockets::Sockets(boost::asio::io_context& io_context,
                 const boost::filesystem::path& endpoint_base_dir,
                 bool listen)
    : base_dir(endpoint_base_dir),
      io_context(io_context),
      host_vst_dispatch(io_context),
      host_vst_dispatch_midi_events(io_context),
      vst_host_callback(io_context),
      host_vst_parameters(io_context),
      host_vst_process_replacing(io_context),
      host_vst_control(io_context),
      host_vst_dispatch_endpoint(
          (base_dir / "host_vst_dispatch.sock").string()),
      host_vst_dispatch_midi_events_endpoint(
          (base_dir / "host_vst_dispatch_midi_events.sock").string()),
      vst_host_callback_endpoint(
          (base_dir / "vst_host_callback.sock").string()),
      host_vst_parameters_endpoint(
          (base_dir / "host_vst_parameters.sock").string()),
      host_vst_process_replacing_endpoint(
          (base_dir / "host_vst_process_replacing.sock").string()),
      host_vst_control_endpoint((base_dir / "host_vst_control.sock").string()) {
    if (listen) {
        fs::create_directory(base_dir);

        acceptors = Acceptors{
            .host_vst_dispatch{io_context, host_vst_dispatch_endpoint},
            .host_vst_dispatch_midi_events{
                io_context, host_vst_dispatch_midi_events_endpoint},
            .vst_host_callback{io_context, vst_host_callback_endpoint},
            .host_vst_parameters{io_context, host_vst_parameters_endpoint},
            .host_vst_process_replacing{io_context,
                                        host_vst_process_replacing_endpoint},
            .host_vst_control{io_context, host_vst_control_endpoint},
        };
    }
}

Sockets::~Sockets() {
    // Only clean if we're the ones who have created these files, although it
    // should not cause any harm to also do this on the Wine side
    if (acceptors) {
        try {
            fs::remove_all(base_dir);
        } catch (const fs::filesystem_error&) {
            // There should not be any filesystem errors since only one side
            // removes the files, but if we somehow can't delete the file then
            // we can just silently ignore this
        }
    }
}

void Sockets::connect() {
    if (acceptors) {
        acceptors->host_vst_dispatch.accept(host_vst_dispatch);
        acceptors->host_vst_dispatch_midi_events.accept(
            host_vst_dispatch_midi_events);
        acceptors->vst_host_callback.accept(vst_host_callback);
        acceptors->host_vst_parameters.accept(host_vst_parameters);
        acceptors->host_vst_process_replacing.accept(
            host_vst_process_replacing);
        acceptors->host_vst_control.accept(host_vst_control);
    } else {
        host_vst_dispatch.connect(host_vst_dispatch_endpoint);
        host_vst_dispatch_midi_events.connect(
            host_vst_dispatch_midi_events_endpoint);
        vst_host_callback.connect(vst_host_callback_endpoint);
        host_vst_parameters.connect(host_vst_parameters_endpoint);
        host_vst_process_replacing.connect(host_vst_process_replacing_endpoint);
        host_vst_control.connect(host_vst_control_endpoint);
    }
}

boost::filesystem::path generate_endpoint_base(const std::string& plugin_name) {
    fs::path temp_directory = get_temporary_directory();

    std::random_device random_device;
    std::mt19937 rng(random_device());
    fs::path candidate_endpoint;
    do {
        std::string random_id;
        std::sample(
            alphanumeric_characters,
            alphanumeric_characters + strlen(alphanumeric_characters) - 1,
            std::back_inserter(random_id), 8, rng);

        // We'll get rid of the file descriptors immediately after accepting the
        // sockets, so putting them inside of a subdirectory would only leave
        // behind an empty directory
        std::ostringstream socket_name;
        socket_name << "yabridge-" << plugin_name << "-" << random_id;

        candidate_endpoint = temp_directory / socket_name.str();
    } while (fs::exists(candidate_endpoint));

    return candidate_endpoint;
}
