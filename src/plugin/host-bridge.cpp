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

#include "host-bridge.h"

#include <boost/asio/read_until.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem.hpp>
#include <boost/process/env.hpp>
#include <boost/process/io.hpp>
#include <boost/process/search_path.hpp>
#include <iostream>
#include <random>

// TODO: I should track down the VST2 SDK for clarification on some of the
//       implementation details, such as the use of intptr_t isntead of void*
//       here.

namespace bp = boost::process;
// I'd rather use std::filesystem instead, but Boost.Process depends on
// boost::filesystem
namespace fs = boost::filesystem;

/**
 * The name of the wine VST host binary.
 */
constexpr auto yabridge_wine_host_name = "yabridge-host.exe";

/**
 * Used for generating random identifiers.
 */
constexpr char alphanumeric_characters[] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

std::string create_logger_prefix(const fs::path& socket_path);
fs::path find_vst_plugin();
fs::path find_wine_vst_host();
std::optional<fs::path> find_wineprefix();
fs::path generate_endpoint_name();
bp::environment set_wineprefix();

intptr_t dispatch_proxy(AEffect*, int32_t, int32_t, intptr_t, void*, float);
void process_proxy(AEffect*, float**, float**, int32_t);
void process_replacing_proxy(AEffect*, float**, float**, int);
void setParameter_proxy(AEffect*, int32_t, float);
float getParameter_proxy(AEffect*, int32_t);

/**
 * Fetch the bridge instance stored in an unused pointer from a VST plugin. This
 * is sadly needed as a workaround to avoid using globals since we need free
 * function pointers to interface with the VST C API.
 */
HostBridge& get_bridge_instance(const AEffect& plugin) {
    return *static_cast<HostBridge*>(plugin.ptr3);
}

HostBridge::HostBridge(audioMasterCallback host_callback)
    : vst_host_path(find_wine_vst_host()),
      vst_plugin_path(find_vst_plugin()),
      io_context(),
      socket_endpoint(generate_endpoint_name().string()),
      socket_acceptor(io_context, socket_endpoint),
      host_vst_dispatch(io_context),
      vst_host_callback(io_context),
      host_vst_parameters(io_context),
      host_vst_process_replacing(io_context),
      vst_host_aeffect(io_context),
      host_callback_function(host_callback),
      logger(Logger::create_from_environment(
          create_logger_prefix(socket_endpoint.path()))),
      wine_stdout(io_context),
      wine_stderr(io_context),
      vst_host(vst_host_path,
               // The Wine VST host needs to know which plugin to load
               // and which Unix domain socket to connect to
               vst_plugin_path,
               socket_endpoint.path(),
               bp::env = set_wineprefix(),
               bp::std_out = wine_stdout,
               bp::std_err = wine_stderr),
      process_buffer(std::make_unique<AudioBuffers::buffer_type>()) {
    logger.log("Initializing yabridge using '" + vst_host_path.string() + "'");
    logger.log("plugin:     '" + vst_plugin_path.string() + "'");
    logger.log("wineprefix: '" +
               find_wineprefix().value_or("<default>").string() + "'");

    // It's very important that these sockets are connected to in the same
    // order in the Wine VST host
    socket_acceptor.accept(host_vst_dispatch);
    socket_acceptor.accept(vst_host_callback);
    socket_acceptor.accept(host_vst_parameters);
    socket_acceptor.accept(host_vst_process_replacing);
    socket_acceptor.accept(vst_host_aeffect);

    // Set up all pointers for our `AEffect` struct. We will fill this with data
    // from the VST plugin loaded in Wine at the end of this constructor.
    plugin.ptr3 = this;
    plugin.dispatcher = dispatch_proxy;
    plugin.process = process_proxy;
    plugin.setParameter = setParameter_proxy;
    plugin.getParameter = getParameter_proxy;
    plugin.processReplacing = process_replacing_proxy;

    // For our communication we use simple threads and blocking operations
    // instead of asynchronous IO since communication has to be handled in
    // lockstep anyway
    host_callback_handler = std::thread([&]() {
        while (true) {
            passthrough_event(vst_host_callback, &plugin,
                              host_callback_function,
                              std::pair<Logger&, bool>(logger, false));
        }
    });
    wine_io_handler = std::thread([&]() { io_context.run(); });

    // Print the Wine host's STDOUT and STDERR streams to the log file
    async_log_pipe_lines(wine_stdout, wine_stdout_buffer, "[Wine STDOUT] ");
    async_log_pipe_lines(wine_stderr, wine_stderr_buffer, "[Wine STDERR] ");

    // Read the plugin's information from the Wine process. This can only be
    // done after we started accepting host callbacks as the plugin might do
    // this during initialization.
    // XXX: If the plugin has crashed then this read should fail instead of
    //      blocking indefinitely, check if this is the case
    plugin = read_object(vst_host_aeffect, plugin);
}

/**
 * Handle an event sent by the VST host. Most of these opcodes will be passed
 * through to the winelib VST host.
 */
intptr_t HostBridge::dispatch(AEffect* /*plugin*/,
                              int32_t opcode,
                              int32_t index,
                              intptr_t value,
                              void* data,
                              float option) {
    // Some events need some extra handling
    // TODO: Handle other things such as GUI itneraction
    switch (opcode) {
        case effClose:
            // TODO: Gracefully close the editor?
            // TODO: Check whether the sockets and the endpoint are closed
            //       correctly
            // XXX: Boost.Process will send SIGKILL to the process for us, is
            //      there a way to manually send a SIGTERM signal instead?

            // The VST API does not have an explicit function for releasing
            // resources, so we'll have to do it here. The actual plugin
            // instance gets freed by the host, or at least I think it does.
            delete this;

            return 0;
            break;
    }

    return send_event(host_vst_dispatch, opcode, index, value, data, option,
                      std::pair<Logger&, bool>(logger, true));
}

void HostBridge::process_replacing(AEffect* /*plugin*/,
                                   float** inputs,
                                   float** outputs,
                                   int sample_frames) {
    // The inputs and outputs arrays should be `[num_inputs][sample_frames]` and
    // `[num_outputs][sample_frames]` floats large respectfully.
    std::vector<std::vector<float>> input_buffers(
        plugin.numInputs, std::vector<float>(sample_frames));
    for (int channel = 0; channel < plugin.numInputs; channel++) {
        std::copy(inputs[channel], inputs[channel] + sample_frames + 1,
                  input_buffers[channel].begin());
    }

    const AudioBuffers request{input_buffers, sample_frames};
    write_object(host_vst_process_replacing, request, *process_buffer);

    // /Write the results back to the `outputs` arrays
    AudioBuffers response;
    response =
        read_object(host_vst_process_replacing, response, *process_buffer);

    // TODO: Doesn't quite work yet, not sure which side is causing problems
    assert(response.buffers.size() == static_cast<size_t>(plugin.numOutputs));
    for (int channel = 0; channel < plugin.numOutputs; channel++) {
        std::copy(response.buffers[channel].begin(),
                  response.buffers[channel].end(), outputs[channel]);
    }
}

float HostBridge::get_parameter(AEffect* /*plugin*/, int32_t index) {
    logger.log_get_parameter(index);

    const Parameter request{index, std::nullopt};
    write_object(host_vst_parameters, request);

    const auto response = read_object<ParameterResult>(host_vst_parameters);
    logger.log_get_parameter_response(response.value.value());

    return response.value.value();
}

void HostBridge::set_parameter(AEffect* /*plugin*/,
                               int32_t index,
                               float value) {
    logger.log_set_parameter(index, value);

    const Parameter request{index, value};
    write_object(host_vst_parameters, request);

    // This should not contain any values and just serve as an acknowledgement
    const auto response = read_object<ParameterResult>(host_vst_parameters);
    logger.log_set_parameter_response();

    assert(!response.value.has_value());
}

void HostBridge::async_log_pipe_lines(bp::async_pipe& pipe,
                                      boost::asio::streambuf& buffer,
                                      std::string prefix) {
    boost::asio::async_read_until(
        pipe, buffer, '\n', [&, prefix](const auto&, size_t) {
            std::string line;
            std::getline(std::istream(&buffer), line);
            logger.log(prefix + line);

            async_log_pipe_lines(pipe, buffer, prefix);
        });
}

/**
 * Create a logger prefix based on the unique socket path for easy
 * identification. The socket path contains both the plugin's name and a unique
 * identifier.
 *
 * @param socket_path The path to the socket endpoint in use.
 *
 * @return A prefix string for log messages.
 */
std::string create_logger_prefix(const fs::path& socket_path) {
    std::ostringstream prefix;
    prefix << "[" << socket_path.filename().replace_extension().string()
           << "] ";

    return prefix.str();
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
    fs::path host_path =
        fs::canonical(boost::dll::this_line_location()).remove_filename() /
        yabridge_wine_host_name;
    if (fs::exists(host_path)) {
        return host_path;
    }

    // Bosot will return an empty path if the file could not be found in the
    // search path
    const fs::path vst_host_path = bp::search_path(yabridge_wine_host_name);
    if (vst_host_path == "") {
        throw std::runtime_error("Could not locate '" +
                                 std::string(yabridge_wine_host_name) + "'");
    }

    return vst_host_path;
}

/**
 * Locate the wineprefix this file is located in, if it is inside of a wine
 * prefix.
 *
 * @return Either the path to the wineprefix (containing the `drive_c?`
 *   directory), or `std::nullopt` if it is not inside of a wine prefix.
 */
std::optional<fs::path> find_wineprefix() {
    // Try to locate the wineprefix this .so file is located in by finding the
    // first parent directory that contains a directory named `dosdevices`
    fs::path wineprefix_path =
        boost::dll::this_line_location().remove_filename();
    while (wineprefix_path != "") {
        if (fs::is_directory(wineprefix_path / "dosdevices")) {
            return wineprefix_path;
        }

        wineprefix_path = wineprefix_path.parent_path();
    }

    return std::nullopt;
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

    // Also resolve symlinks here, mostly for development purposes
    return fs::canonical(plugin_path);
}

/**
 * Generate a unique name for the Unix domain socket endpoint based on the VST
 * plugin's name. This will also generate the parent directory if it does not
 * yet exist since we're using this in the constructor's initializer list.
 *
 * @return A path to a not yet existing Unix domain socket endpoint.
 * @throw std::runtime_error If no matching .dll file could be found.
 */
fs::path generate_endpoint_name() {
    const auto plugin_name =
        find_vst_plugin().filename().replace_extension("").string();

    std::random_device random_device;
    std::mt19937 rng(random_device());
    fs::path candidate_endpoint;
    do {
        std::string random_id;
        std::sample(
            alphanumeric_characters,
            alphanumeric_characters + strlen(alphanumeric_characters) - 1,
            std::back_inserter(random_id), 8, rng);

        candidate_endpoint = fs::temp_directory_path() / "yabridge" /
                             (plugin_name + "-" + random_id + ".sock");
    } while (fs::exists(candidate_endpoint));

    // Ensure that the parent directory exists so the socket endpoint can be
    // created there
    fs::create_directories(candidate_endpoint.parent_path());

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

    const auto wineprefix_path = find_wineprefix();
    if (wineprefix_path.has_value()) {
        env["WINEPREFIX"] = wineprefix_path->string();
    }

    return env;
}

// The below functions are proxy functions for the methods defined in
// `Bridge.cpp`

intptr_t dispatch_proxy(AEffect* plugin,
                        int32_t opcode,
                        int32_t index,
                        intptr_t value,
                        void* data,
                        float option) {
    return get_bridge_instance(*plugin).dispatch(plugin, opcode, index, value,
                                                 data, option);
}

void process_proxy(AEffect* plugin,
                   float** inputs,
                   float** outputs,
                   int32_t sample_frames) {
    return get_bridge_instance(*plugin).process_replacing(
        plugin, inputs, outputs, sample_frames);
}

void process_replacing_proxy(AEffect* plugin,
                             float** inputs,
                             float** outputs,
                             int sample_frames) {
    return get_bridge_instance(*plugin).process_replacing(
        plugin, inputs, outputs, sample_frames);
}

void setParameter_proxy(AEffect* plugin, int32_t index, float value) {
    return get_bridge_instance(*plugin).set_parameter(plugin, index, value);
}

float getParameter_proxy(AEffect* plugin, int32_t index) {
    return get_bridge_instance(*plugin).get_parameter(plugin, index);
}
