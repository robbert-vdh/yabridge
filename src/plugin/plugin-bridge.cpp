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

#include "plugin-bridge.h"

#include <boost/asio/read_until.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem.hpp>
#include <boost/process/env.hpp>
#include <boost/process/io.hpp>
#include <boost/process/search_path.hpp>
#include <boost/process/system.hpp>
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

intptr_t dispatch_proxy(AEffect*, int, int, intptr_t, void*, float);
void process_proxy(AEffect*, float**, float**, int);
void process_replacing_proxy(AEffect*, float**, float**, int);
void setParameter_proxy(AEffect*, int, float);
float getParameter_proxy(AEffect*, int);

/**
 * Create a logger prefix based on the unique socket path for easy
 * identification. The socket path contains both the plugin's name and a unique
 * identifier.
 *
 * @param socket_path The path to the socket endpoint in use.
 *
 * @return A prefix string for log messages.
 */
std::string create_logger_prefix(const fs::path& socket_path);

/**
 * Determine the architecture of a VST plugin (or rather, a .dll file) based on
 * it's header values.
 *
 * See https://docs.microsoft.com/en-us/windows/win32/debug/pe-format for more
 * information on the PE32 format.
 *
 * @param plugin_path The path to the .dll file we're going to check.
 *
 * @return The detected architecture.
 * @throw std::runtime_error If the file is not a .dll file.
 */
PluginArchitecture find_vst_architecture(fs::path);

/**
 * Finds the Wine VST hsot (either `yabridge-host.exe` or `yabridge-host.exe`
 * depending on the plugin). For this we will search in two places:
 *
 *   1. Alongside libyabridge.so if the file got symlinked. This is useful
 *      when developing, as you can simply symlink the the libyabridge.so
 *      file in the build directory without having to install anything to
 *      /usr.
 *   2. In the regular search path.
 *
 * @param plugin_arch The architecture of the plugin, either 64-bit or 32-bit.
 *   Used to determine which host application to use, if available.
 *
 * @return The a path to the VST host, if found.
 * @throw std::runtime_error If the Wine VST host could not be found.
 */
fs::path find_vst_host(PluginArchitecture plugin_arch);

/**
 * Find the VST plugin .dll file that corresponds to this copy of
 * `libyabridge.so`. This should be the same as the name of this file but with a
 * `.dll` file extension instead of `.so`. In case this file does not exist and
 * the `.so` file is a symlink, we'll also repeat this check for the file it
 * links to. This is to support the workflow described in issue #3 where you use
 * symlinks to copies of `libyabridge.so`.
 *
 * @return The a path to the accompanying VST plugin .dll file.
 * @throw std::runtime_error If no matching .dll file could be found.
 */
fs::path find_vst_plugin();

/**
 * Locate the Wine prefix this file is located in, if it is inside of a wine
 * prefix.
 *
 * @return Either the path to the Wine prefix (containing the `drive_c?`
 *   directory), or `std::nullopt` if it is not inside of a wine prefix.
 */
std::optional<fs::path> find_wineprefix();

/**
 * Generate a unique name for the Unix domain socket endpoint based on the VST
 * plugin's name. This will also generate the parent directory if it does not
 * yet exist since we're using this in the constructor's initializer list.
 *
 * @return A path to a not yet existing Unix domain socket endpoint.
 * @throw std::runtime_error If no matching .dll file could be found.
 */
fs::path generate_endpoint_name();

/**
 * Return a path to this `.so` file. This can be used to find out from where
 * this link to or copy of `libyabridge.so` was loaded.
 */
fs::path get_this_file_location();

/**
 * Return the installed Wine version. This is obtained by from `wine --version`
 * and then stripping the `wine-` prefix. This respects the `WINELOADER`
 * environment variable used in the scripts generated by winegcc.
 *
 * This will *not* throw when Wine can not be found, but will instead return
 * '<NOT FOUND>'. This way the user will still get some useful log files.
 */
std::string get_wine_version();

/**
 * Locate the Wine prefix and set the `WINEPREFIX` environment variable if
 * found. This way it's also possible to run .dll files outside of a Wine prefix
 * using the user's default prefix.
 */
bp::environment set_wineprefix();

/**
 * Fetch the bridge instance stored in an unused pointer from a VST plugin. This
 * is sadly needed as a workaround to avoid using globals since we need free
 * function pointers to interface with the VST C API.
 */
PluginBridge& get_bridge_instance(const AEffect& plugin) {
    return *static_cast<PluginBridge*>(plugin.ptr3);
}

PluginBridge::PluginBridge(audioMasterCallback host_callback)
    : vst_plugin_path(find_vst_plugin()),
      vst_plugin_arch(find_vst_architecture(vst_plugin_path)),
      vst_host_path(find_vst_host(vst_plugin_arch)),
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
      wine_version(get_wine_version()),
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
    logger.log("host:         '" + vst_host_path.string() + "'");
    logger.log("plugin:       '" + vst_plugin_path.string() + "'");
    logger.log("socket:       '" + socket_endpoint.path() + "'");
    logger.log("wine prefix:  '" +
               find_wineprefix().value_or("<default>").string() + "'");
    logger.log("wine version: '" + wine_version + "'");

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

    // Print the Wine host's STDOUT and STDERR streams to the log file. This
    // should be done before trying to accept the sockets as otherwise we will
    // miss all output.
    async_log_pipe_lines(wine_stdout, wine_stdout_buffer, "[Wine STDOUT] ");
    async_log_pipe_lines(wine_stderr, wine_stderr_buffer, "[Wine STDERR] ");
    wine_io_handler = std::thread([&]() { io_context.run(); });

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
                // TODO: Think of a nicer way to structure this and the similar
                //       handler in `WineBridge::handle_dispatch_midi_events`
                receive_event(
                    vst_host_callback, std::pair<Logger&, bool>(logger, false),
                    [&](Event& event) {
                        // MIDI events sent from the plugin back to the host are
                        // a special case here. They have to sent during the
                        // `processReplacing()` function or else the host will
                        // ignore them. Because of this we'll temporarily save
                        // any MIDI events we receive here, and then we'll
                        // actually send them to the host at the end of the
                        // `process_replacing()` function.
                        if (event.opcode == audioMasterProcessEvents) {
                            std::lock_guard lock(incoming_midi_events_mutex);

                            incoming_midi_events.push_back(
                                std::get<DynamicVstEvents>(event.payload));
                            EventResult response{1, nullptr, std::nullopt};

                            return response;
                        } else {
                            return passthrough_event(
                                &plugin, host_callback_function)(event);
                        }
                    });
            }
        } catch (const boost::system::system_error&) {
            // This happens when the sockets got closed because the plugin
            // is being shut down
        }
    });

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
                break;
            case effSetSpeakerArrangement:
            case effGetSpeakerArrangement:
                // This is the output speaker configuration, the `read_value()`
                // method below reads the input speaker configuration
                return DynamicSpeakerArrangement(
                    *static_cast<const VstSpeakerArrangement*>(data));
                break;
            // Any VST host I've encountered has properly zeroed out these their
            // string buffers, but we'll add a list of opcodes that should
            // return a string just in case `DefaultDataConverter::read()` can't
            // figure it out.
            case effGetProgramName:
            case effGetParamLabel:
            case effGetParamDisplay:
            case effGetParamName:
            case effGetProgramNameIndexed:
            case effGetEffectName:
            case effGetVendorString:
            case effGetProductString:
            case effShellGetNextPlugin:
                return WantsString{};
                break;
            default:
                return DefaultDataConverter::read(opcode, index, value, data);
                break;
        }
    }

    std::optional<EventPayload> read_value(const int opcode,
                                           const intptr_t value) {
        switch (opcode) {
            case effSetSpeakerArrangement:
            case effGetSpeakerArrangement:
                // These two events are special in that they pass a pointer to
                // the output speaker configuration through the `data`
                // parameter, but then they also pass a pointer to the input
                // speaker configuration through the `value` parameter. This is
                // the only event that does this.
                return DynamicSpeakerArrangement(
                    *static_cast<const VstSpeakerArrangement*>(
                        reinterpret_cast<void*>(value)));
                break;
            default:
                return DefaultDataConverter::read_value(opcode, value);
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
                // `PluginBridge` and write a pointer to that struct to the data
                // pointer
                const auto buffer =
                    std::get<std::vector<uint8_t>>(response.payload);
                chunk.assign(buffer.begin(), buffer.end());

                *static_cast<uint8_t**>(data) = chunk.data();
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
            case effGetSpeakerArrangement: {
                // The plugin will have updated the objects passed by the host
                // with its preferred output speaker configuration if it
                // supports this. The same thing happens for the input speaker
                // configuration in `write_value()`.
                auto speaker_arrangement =
                    std::get<DynamicSpeakerArrangement>(response.payload);

                // Reconstruct a dynamically sized `VstSpeakerArrangement`
                // object to a buffer, and write back the results to the data
                // parameter.
                VstSpeakerArrangement* output =
                    static_cast<VstSpeakerArrangement*>(data);
                std::vector<uint8_t> reconstructed_object =
                    speaker_arrangement.as_raw_data();
                std::copy(reconstructed_object.begin(),
                          reconstructed_object.end(),
                          reinterpret_cast<uint8_t*>(output));
            } break;
            default:
                DefaultDataConverter::write(opcode, data, response);
                break;
        }
    }

    intptr_t return_value(const int opcode, const intptr_t original) {
        return DefaultDataConverter::return_value(opcode, original);
    }

    void write_value(const int opcode,
                     intptr_t value,
                     const EventResult& response) {
        switch (opcode) {
            case effGetSpeakerArrangement: {
                // Same as the above, but now for the input speaker
                // configuration object under the `value` pointer
                auto speaker_arrangement =
                    std::get<DynamicSpeakerArrangement>(response.payload);

                VstSpeakerArrangement* output =
                    static_cast<VstSpeakerArrangement*>(
                        reinterpret_cast<void*>(value));
                std::vector<uint8_t> reconstructed_object =
                    speaker_arrangement.as_raw_data();
                std::copy(reconstructed_object.begin(),
                          reconstructed_object.end(),
                          reinterpret_cast<uint8_t*>(output));
            } break;
            default:
                return DefaultDataConverter::write_value(opcode, value,
                                                         response);
                break;
        }
    }

   private:
    std::vector<uint8_t>& chunk;
    VstRect& rect;
};

intptr_t PluginBridge::dispatch(AEffect* /*plugin*/,
                                int opcode,
                                int index,
                                intptr_t value,
                                void* data,
                                float option) {
    // HACK: Ardour 5.X has a bug in its VST implementation where it calls the
    //       plugin's dispatcher before the plugin has even finished
    //       initializing. This has been fixed back in 2018, but there has not
    //       been a release that contains the fix yet. This should be removed
    //       once Ardour 6.0 gets released.
    //       https://tracker.ardour.org/view.php?id=7668
    if (BOOST_UNLIKELY(plugin.magic == 0)) {
        logger.log_event(true, opcode, index, value, nullptr, option);
        logger.log(
            "   WARNING: The host has dispatched an event before the plugin "
            "has finished initializing, ignoring the event. (are we running "
            "Ardour 5.X?)");
        logger.log_event_response(true, opcode, 0, nullptr);
        return 0;
    }

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

            // Boost.Process will send SIGKILL to the Wine host for us when this
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
        case effCanDo: {
            const std::string query(static_cast<const char*>(data));

            // NOTE: If the plugins returns `0xbeefXXXX` to this query, then
            //       REAPER will pass a libSwell handle rather than an X11
            //       window ID to `effEditOpen`. This is of course not going to
            //       work when the GUI is handled using Wine so we'll ignore it.
            if (query == "hasCockosViewAsConfig") {
                logger.log_event(true, opcode, index, value, query, option);
                logger.log(
                    "   The host requests libSwell GUI support which is not "
                    "supported using Wine, ignoring the request.");
                logger.log_event_response(true, opcode, -1, nullptr);
                return -1;
            }
        } break;
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

void PluginBridge::process_replacing(AEffect* /*plugin*/,
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

    // Plugins are allowed to send MIDI events during processing using a host
    // callback. These have to be processed during the actual
    // `processReplacing()` function or else the hsot will ignore them. To
    // prevent these events from getting delayed by a sample we'll process them
    // after the plugin is done processing audio rather than during the time
    // we're still waiting on the plugin.
    std::lock_guard lock(incoming_midi_events_mutex);
    for (DynamicVstEvents& events : incoming_midi_events) {
        host_callback_function(&plugin, audioMasterProcessEvents, 0, 0,
                               &events.as_c_events(), 0.0);
    }

    incoming_midi_events.clear();
}

float PluginBridge::get_parameter(AEffect* /*plugin*/, int index) {
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

void PluginBridge::set_parameter(AEffect* /*plugin*/, int index, float value) {
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

void PluginBridge::async_log_pipe_lines(patched_async_pipe& pipe,
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

std::optional<fs::path> find_wineprefix() {
    // Try to locate the Wine prefix the plugin's .dll file is located in by
    // finding the first parent directory that contains a directory named
    // `dosdevices`
    fs::path wineprefix_path = find_vst_plugin();
    while (wineprefix_path != "") {
        if (fs::is_directory(wineprefix_path / "dosdevices")) {
            return wineprefix_path;
        }

        wineprefix_path = wineprefix_path.parent_path();
    }

    return std::nullopt;
}

PluginArchitecture find_vst_architecture(fs::path plugin_path) {
    std::ifstream file(plugin_path, std::ifstream::binary | std::ifstream::in);

    // The linker will place the offset where the PE signature is placed at the
    // end of the MS-DOS stub, at offset 0x3c
    uint32_t pe_signature_offset;
    file.seekg(0x3c);
    file.read(reinterpret_cast<char*>(&pe_signature_offset),
              sizeof(pe_signature_offset));

    // The PE32 signature will be followed by a magic number that indicates the
    // target architecture of the binary
    uint32_t pe_signature;
    uint16_t machine_type;
    file.seekg(pe_signature_offset);
    file.read(reinterpret_cast<char*>(&pe_signature), sizeof(pe_signature));
    file.read(reinterpret_cast<char*>(&machine_type), sizeof(machine_type));

    constexpr char expected_pe_signature[4] = {'P', 'E', '\0', '\0'};
    if (pe_signature !=
        *reinterpret_cast<const uint32_t*>(expected_pe_signature)) {
        throw std::runtime_error("'" + plugin_path.string() +
                                 "' is not a valid .dll file");
    }

    // These constants are specified in
    // https://docs.microsoft.com/en-us/windows/win32/debug/pe-format#machine-types
    switch (machine_type) {
        case 0x014c:  // IMAGE_FILE_MACHINE_I386
            return PluginArchitecture::vst_32;
            break;
        case 0x8664:  // IMAGE_FILE_MACHINE_AMD64
        case 0x0000:  // IMAGE_FILE_MACHINE_UNKNOWN
            return PluginArchitecture::vst_64;
            break;
    }

    // When compiled without optimizations, GCC 9.3 will warn that the function
    // does not return if we put this in a `default:` case instead.
    std::ostringstream error_msg;
    error_msg << "'" << plugin_path
              << "' is neither a x86 nor a x86_64 PE32 file. Actual "
                 "architecture: 0x"
              << std::hex << machine_type;
    throw std::runtime_error(error_msg.str());
}

fs::path find_vst_host(PluginArchitecture plugin_arch) {
    auto host_name = yabridge_wine_host_name;
    if (plugin_arch == PluginArchitecture::vst_32) {
        host_name = yabridge_wine_host_name_32bit;
    }

    fs::path host_path =
        fs::canonical(get_this_file_location()).remove_filename() / host_name;
    if (fs::exists(host_path)) {
        return host_path;
    }

    // Bosot will return an empty path if the file could not be found in the
    // search path
    const fs::path vst_host_path = bp::search_path(host_name);
    if (vst_host_path == "") {
        throw std::runtime_error("Could not locate '" + std::string(host_name) +
                                 "'");
    }

    return vst_host_path;
}

fs::path find_vst_plugin() {
    const fs::path this_plugin_path =
        "/" / fs::path("/" + get_this_file_location().string());

    fs::path plugin_path(this_plugin_path);
    plugin_path.replace_extension(".dll");
    if (fs::exists(plugin_path)) {
        // Also resolve symlinks here, to support symlinked .dll files
        return fs::canonical(plugin_path);
    }

    // In case this files does not exist and our `.so` file is a symlink, we'll
    // also repeat this check after resolving that symlink to support links to
    // copies of `libyabridge.so` as described in issue #3
    fs::path alternative_plugin_path = fs::canonical(this_plugin_path);
    alternative_plugin_path.replace_extension(".dll");
    if (fs::exists(alternative_plugin_path)) {
        return fs::canonical(alternative_plugin_path);
    }

    // This function is used in the constructor's initializer list so we have to
    // throw when the path could not be found
    throw std::runtime_error("'" + plugin_path.string() +
                             "' does not exist, make sure to rename "
                             "'libyabridge.so' to match a "
                             "VST plugin .dll file.");
}

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

fs::path get_this_file_location() {
    // HACK: Not sure why, but `boost::dll::this_line_location()` returns a path
    //       starting with a double slash on some systems. I've seen this happen
    //       on both Ubuntu 18.04 and 20.04, but not on Arch based distros.
    //       Under Linux a path starting with two slashes is treated the same as
    //       a path starting with only a single slash, but Wine will refuse to
    //       load any files when the path starts with two slashes. Prepending
    //       `/` to a pad coerces theses two slashes into a single slash.
    return "/" / boost::dll::this_line_location();
}

std::string get_wine_version() {
    // The '*.exe' scripts generated by winegcc allow you to override the binary
    // used to run Wine, so will will respect this as well
    std::string wine_command = "wine";

    bp::native_environment env = boost::this_process::environment();
    if (!env["WINELOADER"].empty()) {
        wine_command = env.get("WINELOADER");
    }

    bp::ipstream output;
    try {
        const fs::path wine_path = bp::search_path(wine_command);
        bp::system(wine_path, "--version", bp::std_out = output);
    } catch (const std::system_error&) {
        return "<NOT FOUND>";
    }

    // `wine --version` might contain additional output in certain custom Wine
    // builds, so we only want to look at the first line
    std::string version_string;
    std::getline(output, version_string);

    // Strip the `wine-` prefix from the output, could potentially be absent in
    // custom Wine builds
    const std::string version_prefix("wine-");
    if (version_string.find(version_prefix) == 0) {
        version_string = version_string.substr(version_prefix.size());
    }

    return version_string;
}

bp::environment set_wineprefix() {
    bp::native_environment env = boost::this_process::environment();

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
