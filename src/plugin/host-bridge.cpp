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

#ifdef USE_WINEDBG
#include <boost/process/start_dir.hpp>
#endif

// Generated inside of build directory
#include <src/common/config/config.h>
#include <src/common/config/version.h>

#include "../common/communication.h"
#include "../common/events.h"

namespace bp = boost::process;
// I'd rather use std::filesystem instead, but Boost.Process depends on
// boost::filesystem
namespace fs = boost::filesystem;

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

intptr_t dispatch_proxy(AEffect*, int, int, intptr_t, void*, float);
void process_proxy(AEffect*, float**, float**, int);
void process_replacing_proxy(AEffect*, float**, float**, int);
void setParameter_proxy(AEffect*, int, float);
float getParameter_proxy(AEffect*, int);

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
      // All the fields should be zero initialized because
      // `Vst2PluginInstance::vstAudioMasterCallback` from Bitwig's plugin
      // bridge will crash otherwise
      plugin(),
      io_context(),
      socket_endpoint(generate_endpoint_name().string()),
      socket_acceptor(io_context, socket_endpoint),
      host_vst_dispatch(io_context),
      host_vst_dispatch_midi_events(io_context),
      vst_host_callback(io_context),
      host_vst_parameters(io_context),
      host_vst_process_replacing(io_context),
      vst_host_aeffect(io_context),
      host_callback_function(host_callback),
      logger(Logger::create_from_environment(
          create_logger_prefix(socket_endpoint.path()))),
      wine_stdout(io_context),
      wine_stderr(io_context),
#ifndef USE_WINEDBG
      vst_host(vst_host_path,
               // The Wine VST host needs to know which plugin to load
               // and which Unix domain socket to connect to
               vst_plugin_path,
               socket_endpoint.path(),
               bp::env = set_wineprefix(),
               bp::std_out = wine_stdout,
               bp::std_err = wine_stderr)
#else
      // This is set up for KDE Plasma. Other desktop environments and window
      // managers require some slight modifications to spawn a detached terminal
      // emulator.
      vst_host("/usr/bin/kstart5",
               "konsole",
               "--",
               "-e",
               "winedbg",
               "--gdb",
               vst_host_path.string() + ".so",
               vst_plugin_path.filename(),
               socket_endpoint.path(),
               bp::env = set_wineprefix(),
               // winedbg has no reliable way to escape spaces, so we'll start
               // the process in the plugin's directory
               bp::start_dir = vst_plugin_path.parent_path())
#endif
{
    logger.log("Initializing yabridge version " +
               std::string(yabridge_git_version));
    logger.log("host:       '" + vst_host_path.string() + "'");
    logger.log("plugin:     '" + vst_plugin_path.string() + "'");
    logger.log("socket:     '" + socket_endpoint.path() + "'");
    logger.log("wineprefix: '" +
               find_wineprefix().value_or("<default>").string() + "'");

    // Include a list of enabled compile-tiem features, mostly to make debug
    // logs more useful
    logger.log("");
    logger.log("Enabled features:");
#ifdef USE_BITBRIDGE
    logger.log("- bitbridge support");
#endif
#ifdef USE_WINEDBG
    logger.log("- winedbg");
#endif
#if !(defined(USE_BITBRIDGE) || defined(USE_WINEDBG))
    logger.log("  <none>");
#endif
    logger.log("");

    // It's very important that these sockets are connected to in the same
    // order in the Wine VST host
    socket_acceptor.accept(host_vst_dispatch);
    socket_acceptor.accept(host_vst_dispatch_midi_events);
    socket_acceptor.accept(vst_host_callback);
    socket_acceptor.accept(host_vst_parameters);
    socket_acceptor.accept(host_vst_process_replacing);
    socket_acceptor.accept(vst_host_aeffect);

    // There's no need to keep the socket endpoint file around after accepting
    // all the sockets, and RAII won't clean these files up for us
    socket_acceptor.close();
    fs::remove(socket_endpoint.path());

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
        try {
            while (true) {
                passthrough_event(vst_host_callback,
                                  std::pair<Logger&, bool>(logger, false),
                                  &plugin, host_callback_function);
            }
        } catch (const boost::system::system_error&) {
            // This happens when the sockets got closed because the plugin
            // is being shut down
        }
    });

    // Print the Wine host's STDOUT and STDERR streams to the log file
    async_log_pipe_lines(wine_stdout, wine_stdout_buffer, "[Wine STDOUT] ");
    async_log_pipe_lines(wine_stderr, wine_stderr_buffer, "[Wine STDERR] ");
    wine_io_handler = std::thread([&]() { io_context.run(); });

    // Read the plugin's information from the Wine process. This can only be
    // done after we started accepting host callbacks as the plugin might do
    // this during initialization.
    plugin = read_object(vst_host_aeffect, plugin);
}

class DispatchDataConverter : DefaultDataConverter {
   public:
    DispatchDataConverter(std::vector<uint8_t>& chunk_data,
                          VstRect& editor_rectangle)
        : chunk(chunk_data), rect(editor_rectangle) {}

    EventPayload read(const int opcode,
                      const int index,
                      const intptr_t value,
                      const void* data) {
        // There are some events that need specific structs that we can't simply
        // serialize as a string because they might contain null bytes
        switch (opcode) {
            case effEditGetRect:
                return WantsVstRect();
                break;
            case effEditOpen:
                // The host will have passed us an X11 window handle in the void
                // pointer. In the Wine VST host we'll create a Win32 window,
                // ask the plugin to embed itself in that and then embed that
                // window into this X11 window handle.
                return reinterpret_cast<size_t>(data);
                break;
            case effGetChunk:
                return WantsChunkBuffer();
                break;
            case effSetChunk: {
                const uint8_t* chunk_data = static_cast<const uint8_t*>(data);

                // When the host passes a chunk it will use the value parameter
                // to tell us its length
                return std::vector<uint8_t>(chunk_data, chunk_data + value);
            } break;
            case effProcessEvents:
                return DynamicVstEvents(*static_cast<const VstEvents*>(data));
                break;
            case effGetInputProperties:
            case effGetOutputProperties:
                // In this case we can't simply pass an empty marker struct
                // because the host can have already populated this field with
                // data (or at least Bitwig does this)
                return *static_cast<const VstIOProperties*>(data);
                break;
            case effGetParameterProperties:
                return *static_cast<const VstParameterProperties*>(data);
                break;
            case effGetMidiKeyName:
                return *static_cast<const VstMidiKeyName*>(data);
            default:
                return DefaultDataConverter::read(opcode, index, value, data);
                break;
        }
    }

    void write(const int opcode, void* data, const EventResult& response) {
        switch (opcode) {
            case effEditGetRect: {
                // Write back the (hopefully) updated editor dimensions
                const auto new_rect = std::get<VstRect>(response.payload);
                rect = new_rect;

                *static_cast<VstRect**>(data) = &rect;
            } break;
            case effGetChunk: {
                // Write the chunk data to some publically accessible place in
                // `HostBridge` and write a pointer to that struct to the data
                // pointer
                const auto buffer =
                    std::get<std::vector<uint8_t>>(response.payload);
                chunk.assign(buffer.begin(), buffer.end());

                *static_cast<void**>(data) = chunk.data();
            } break;
            case effGetInputProperties:
            case effGetOutputProperties: {
                // These opcodes pass the plugin some empty struct through the
                // data parameter that the plugin then fills with flags and
                // other data to describe an input or output channel.
                const auto properties =
                    std::get<VstIOProperties>(response.payload);

                *static_cast<VstIOProperties*>(data) = properties;
            } break;
            case effGetParameterProperties: {
                // Same as the above
                const auto properties =
                    std::get<VstParameterProperties>(response.payload);

                *static_cast<VstParameterProperties*>(data) = properties;
            } break;
            case effGetMidiKeyName: {
                // Ditto
                const auto properties =
                    std::get<VstMidiKeyName>(response.payload);

                *static_cast<VstMidiKeyName*>(data) = properties;
            } break;
            default:
                DefaultDataConverter::write(opcode, data, response);
                break;
        }
    }

    intptr_t return_value(const int opcode, const intptr_t original) {
        return DefaultDataConverter::return_value(opcode, original);
    }

   private:
    std::vector<uint8_t>& chunk;
    VstRect& rect;
};

/**
 * Handle an event sent by the VST host. Most of these opcodes will be passed
 * through to the winelib VST host.
 */
intptr_t HostBridge::dispatch(AEffect* /*plugin*/,
                              int opcode,
                              int index,
                              intptr_t value,
                              void* data,
                              float option) {
    DispatchDataConverter converter(chunk_data, editor_rectangle);

    switch (opcode) {
        case effClose: {
            // Allow the plugin to handle its own shutdown. I've found a few
            // plugins that work fine except for that they crash during
            // shutdown. This shouldn't have any negative side effects since
            // state has already been saved before this and all resources are
            // cleaned up properly. Still not sure if this is a good way to
            // handle this.
            intptr_t return_value = 0;
            try {
                // TODO: Add some kind of timeout?
                return_value =
                    send_event(host_vst_dispatch, dispatch_mutex, converter,
                               std::pair<Logger&, bool>(logger, true), opcode,
                               index, value, data, option);
            } catch (const boost::system::system_error& a) {
                // Thrown when the socket gets closed because the VST plugin
                // loaded into the Wine process crashed during shutdown
                logger.log("The plugin crashed during shutdown, ignoring");
            }

            // Boost.Process will send SIGKILL to the Wien host for us when this
            // class gets destroyed. Because the process is running a few
            // threads Wine will say something about a segfault (probably
            // related to `std::terminate`), but this doesn't seem to have any
            // negative impact

            // The `stop()` method will cause the IO context to just drop
            // all of its work and immediately and not throw any exceptions
            // that would have been caused by pipes and sockets being closed
            io_context.stop();

            // `std::thread`s are not interruptable, and since we're doing
            // blocking synchronous reads there's no way to interrupt them. If
            // we don't detach them then the runtime will call `std::terminate`
            // for us. The workaround here is to simply detach the threads and
            // then close all sockets. This will cause them to throw exceptions
            // which we then catch and ignore. Please let me know if there's a
            // better way to handle this.q
            host_callback_handler.detach();
            wine_io_handler.detach();

            delete this;

            return return_value;
        }; break;
        case effProcessEvents:
            // Because of limitations of the Win32 API we have to use a seperate
            // thread and socket to pass MIDI events. Otherwise plugins will
            // stop receiving MIDI data when they have an open dropdowns or
            // message box.
            return send_event(host_vst_dispatch_midi_events,
                              dispatch_midi_events_mutex, converter,
                              std::pair<Logger&, bool>(logger, true), opcode,
                              index, value, data, option);
            break;
    }

    // We don't reuse any buffers here like we do for audio processing. This
    // would be useful for chunk data, but since that's only needed when saving
    // and loading plugin state it's much better to have bitsery or our
    // receiving function temporarily allocate a large enough buffer rather than
    // to have a bunch of allocated memory sitting around doing nothing.
    return send_event(host_vst_dispatch, dispatch_mutex, converter,
                      std::pair<Logger&, bool>(logger, true), opcode, index,
                      value, data, option);
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
    write_object(host_vst_process_replacing, request, process_buffer);

    // Write the results back to the `outputs` arrays
    AudioBuffers response;
    response =
        read_object(host_vst_process_replacing, response, process_buffer);

    assert(response.buffers.size() == static_cast<size_t>(plugin.numOutputs));
    for (int channel = 0; channel < plugin.numOutputs; channel++) {
        std::copy(response.buffers[channel].begin(),
                  response.buffers[channel].end(), outputs[channel]);
    }
}

float HostBridge::get_parameter(AEffect* /*plugin*/, int index) {
    logger.log_get_parameter(index);

    const Parameter request{index, std::nullopt};
    ParameterResult response;

    // Prevent race conditions from `getParameter()` and `setParameter()` being
    // called at the same time since  they share the same socket
    {
        std::lock_guard lock(parameters_mutex);
        write_object(host_vst_parameters, request);
        response = read_object<ParameterResult>(host_vst_parameters);
    }

    logger.log_get_parameter_response(response.value.value());

    return response.value.value();
}

void HostBridge::set_parameter(AEffect* /*plugin*/, int index, float value) {
    logger.log_set_parameter(index, value);

    const Parameter request{index, value};
    ParameterResult response;

    {
        std::lock_guard lock(parameters_mutex);
        write_object(host_vst_parameters, request);

        response = read_object<ParameterResult>(host_vst_parameters);
    }

    logger.log_set_parameter_response();

    // This should not contain any values and just serve as an acknowledgement
    assert(!response.value.has_value());
}

void HostBridge::async_log_pipe_lines(patched_async_pipe& pipe,
                                      boost::asio::streambuf& buffer,
                                      std::string prefix) {
    boost::asio::async_read_until(
        pipe, buffer, '\n', [&, prefix](const auto&, size_t) {
            std::string line;
            std::getline(std::istream(&buffer), line);
            logger.log(prefix + line);

            // Not sure why, but this async read will keep reading a ton of
            // empty lines after the Wine process crashes
            if (vst_host.running()) {
                async_log_pipe_lines(pipe, buffer, prefix);
            }
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
    // Use the socket filename as the logger prefix, but strip the `yabridge-`
    // part since that's redundant
    std::string socket_name =
        socket_path.filename().replace_extension().string();
    const std::string socket_prefix("yabridge-");
    assert(socket_name.find(socket_prefix) == 0);
    socket_name = socket_name.substr(socket_prefix.size());

    std::ostringstream prefix;
    prefix << "[" << socket_name << "] ";

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

    // TODO: First, check whether the plugin is 32-bit or 64-bit, and then
    //       search for the correct binary accordingly
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

        // We'll get rid of the file descriptors immediately after accepting the
        // sockets, so putting them inside of a subdirectory would only leave
        // behind an empty directory
        std::ostringstream socket_name;
        socket_name << "yabridge-" << plugin_name << "-" << random_id
                    << ".sock";

        candidate_endpoint = fs::temp_directory_path() / socket_name.str();
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
    auto env = boost::this_process::environment();

    // Allow the wine prefix to be overridden manually
    if (!env["WINEPREFIX"].empty()) {
        return env;
    }

    const auto wineprefix_path = find_wineprefix();
    if (wineprefix_path.has_value()) {
        env["WINEPREFIX"] = wineprefix_path->string();
    }

    return env;
}

// The below functions are proxy functions for the methods defined in
// `Bridge.cpp`

intptr_t dispatch_proxy(AEffect* plugin,
                        int opcode,
                        int index,
                        intptr_t value,
                        void* data,
                        float option) {
    return get_bridge_instance(*plugin).dispatch(plugin, opcode, index, value,
                                                 data, option);
}

void process_proxy(AEffect* plugin,
                   float** inputs,
                   float** outputs,
                   int sample_frames) {
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

void setParameter_proxy(AEffect* plugin, int index, float value) {
    return get_bridge_instance(*plugin).set_parameter(plugin, index, value);
}

float getParameter_proxy(AEffect* plugin, int index) {
    return get_bridge_instance(*plugin).get_parameter(plugin, index);
}
