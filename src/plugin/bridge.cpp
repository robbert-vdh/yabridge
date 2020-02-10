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

#include "bridge.h"

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem.hpp>
#include <boost/process/env.hpp>
#include <boost/process/io.hpp>
#include <boost/process/search_path.hpp>
#include <iostream>
#include <msgpack.hpp>

#include "../common/communication.h"

// TODO: I should track down the VST2 SDK for clarification on some of the
//       implementation details, such as the use of intptr_t isntead of void*
//       here.

namespace bp = boost::process;
namespace fs = boost::filesystem;

/**
 * The name of the wine VST host binary.
 */
constexpr auto yabridge_wine_host_name = "yabridge-host.exe";

fs::path find_vst_plugin();
fs::path find_wine_vst_host();
fs::path generate_endpoint_name();
bp::environment set_wineprefix();

// TODO: When adding debug information, print both the path to the VST host and
//       the chosen wineprefix
Bridge::Bridge()
    : io_context(),
      socket_endpoint(generate_endpoint_name().string()),
      host_vst_dispatch(io_context),
      vst_stdin(),
      vst_stdout(),
      vst_host(find_wine_vst_host(),
               bp::std_in = vst_stdin,
               bp::std_out = vst_stdout,
               bp::env = set_wineprefix()) {
    boost::asio::local::stream_protocol::acceptor acceptor(io_context,
                                                           socket_endpoint);

    // It's very important that these sockets are connected to in the same order
    // in the Wine VST host
    acceptor.accept(host_vst_dispatch);
}

/**
 * Handle an event sent by the VST host. Most of these opcodes will be passed
 * through to the winelib VST host.
 */
intptr_t Bridge::dispatch(AEffect* /*plugin*/,
                          int32_t opcode,
                          int32_t parameter,
                          intptr_t value,
                          void* result,
                          float option) {
    // Some events need some extra handling
    // TODO: Handle other things such as GUI itneraction
    switch (opcode) {
        case effClose:
            // TODO: Gracefully close the editor?
            // XXX: Boost.Process will send SIGKILL to the process for us, is
            //      there a way to manually send a SIGTERM signal instead?

            // The VST API does not have an explicit function for releasing
            // resources, so we'll have to do it here. The actual plugin
            // instance gets freed by the host, or at least I think it does.
            delete this;

            return 0;
            break;
    }

    Event event{opcode, parameter, value, option};
    write_object(vst_stdin, event);

    auto response = read_object<EventResult>(vst_stdout);
    if (response.result) {
        std::copy(response.result->begin(), response.result->end(),
                  static_cast<char*>(result));
    }

    return response.return_value;
}

void Bridge::process(AEffect* /*plugin*/,
                     float** /*inputs*/,
                     float** /*outputs*/,
                     int32_t /*sample_frames*/) {
    // TODO: Unimplmemented
}

void Bridge::set_parameter(AEffect* /*plugin*/,
                           int32_t /*index*/,
                           float /*value*/) {
    // TODO: Unimplmemented
}

float Bridge::get_parameter(AEffect* /*plugin*/, int32_t /*index*/
) {
    // TODO: Unimplmemented
    return 0.0f;
}

/**
 * Finds the Wine VST hsot (named `yabridge-host.exe`). For this we will search
 * in two places:
 *
 *   1. Alongside libyabridge.so if the file got symlinked. This is useful
 *      when developing, as you can simply symlink the the libyabridge.so
 *      file in the build directory without having to install anything to
 *      /usr.
 *   2. In the regular search path.
 *
 * @return The a path to the VST host, if found.
 * @throw std::runtime_error If the Wine VST host could not be found.
 */
fs::path find_wine_vst_host() {
    fs::path host_path = fs::canonical(boost::dll::this_line_location());
    host_path.remove_filename().append(yabridge_wine_host_name);
    if (fs::exists(host_path)) {
        return host_path;
    }

    // Bosot will return an empty path if the file could not be found in the
    // search path
    fs::path vst_host_path = bp::search_path(yabridge_wine_host_name);
    if (vst_host_path == "") {
        throw std::runtime_error("Could not locate '" +
                                 std::string(yabridge_wine_host_name) + "'");
    }

    return vst_host_path;
}

/**
 * Find the VST plugin .dll file that corresponds to this copy of
 * `libyabridge.so`. This should be the same as the name of this file but with a
 * `.dll` file extension instead of `.so`.
 *
 * @return The a path to the accompanying VST plugin .dll file.
 * @throw std::runtime_error If no matching .dll file could be found.
 */
fs::path find_vst_plugin() {
    fs::path plugin_path = boost::dll::this_line_location();
    plugin_path.replace_extension(".dll");

    // This function is used in the constructor's initializer list so we have to
    // throw when the path could not be found
    if (!fs::exists(plugin_path)) {
        throw std::runtime_error(
            "'" + plugin_path.string() +
            "' does not exist, make sure to rename 'libyabridge.so' to match a "
            "VST plugin .dll file.");
    }

    return plugin_path;
}

/**
 * Generate a unique name for the Unix domain socket endpoint based on the VST
 * plugin's name.
 *
 * @return A path to a not yet existing Unix domain socket endpoint.
 * @throw std::runtime_error If no matching .dll file could be found.
 */
fs::path generate_endpoint_name() {
    auto plugin_name =
        find_vst_plugin().filename().replace_extension("").string();

    fs::path candidate_endpoint;
    do {
        std::string random_id("TODO");
        candidate_endpoint =
            fs::temp_directory_path()
                .append("yabridge")
                .append(plugin_name + "-" + random_id + ".sock");
    } while (fs::exists(candidate_endpoint));

    // TODO: Should probably try creating the endpoint right here and catch any
    //       exceptions since this could technically result in a race condition
    //       when two instances of yabridge decide to use the same endpoint name
    //       at the same time

    return candidate_endpoint;
}

/**
 * Locate the wineprefix and set the `WINEPREFIX` environment variable if found.
 * This way it's also possible to run .dll files outside of a wineprefix using
 * the user's default prefix.
 */
bp::environment set_wineprefix() {
    auto env(boost::this_process::environment());

    // Try to locate the wineprefix this .so file is located in by finding the
    // first parent directory that contains a directory named `dosdevices`
    fs::path wineprefix_path =
        boost::dll::this_line_location().remove_filename();
    while (wineprefix_path != "") {
        if (fs::is_directory(fs::path(wineprefix_path).append("dosdevices"))) {
            env["WINEPREFIX"] = wineprefix_path.string();
            break;
        }

        wineprefix_path = wineprefix_path.parent_path();
    }

    return env;
}
