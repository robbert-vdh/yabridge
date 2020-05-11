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
#include <boost/process/env.hpp>
#include <boost/process/io.hpp>
#include <iostream>

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

    // If the Wine process fails to start, then nothing will connect to the
    // sockets and we'll be hanging here indefinitely. To prevent this, we'll
    // periodically poll whether the Wine process is still running, and throw
    // when it is not. The alternative would be to rewrite this to using
    // `async_accept`, Boost.Asio timers, and another IO context, but I feel
    // like this a much simpler solution.
    std::thread([&]() {
        using namespace std::literals::chrono_literals;

        while (true) {
            if (finished_accepting_sockets) {
                return;
            }
            if (!vst_host.running()) {
                throw std::runtime_error(
                    "The Wine process failed to start. Check the output above "
                    "for more information.");
            }

            std::this_thread::sleep_for(1s);
        }
    }).detach();

    // It's very important that these sockets are connected to in the same
    // order in the Wine VST host
    socket_acceptor.accept(host_vst_dispatch);
    socket_acceptor.accept(host_vst_dispatch_midi_events);
    socket_acceptor.accept(vst_host_callback);
    socket_acceptor.accept(host_vst_parameters);
    socket_acceptor.accept(host_vst_process_replacing);
    socket_acceptor.accept(vst_host_aeffect);
    finished_accepting_sockets = true;

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
    const auto initialized_plugin = read_object<AEffect>(vst_host_aeffect);
    update_aeffect(plugin, initialized_plugin);
}

class DispatchDataConverter : DefaultDataConverter {
   public:
    DispatchDataConverter(std::vector<uint8_t>& chunk_data,
                          AEffect& plugin,
                          VstRect& editor_rectangle)
        : chunk(chunk_data), plugin(plugin), rect(editor_rectangle) {}

    EventPayload read(const int opcode,
                      const int index,
                      const intptr_t value,
                      const void* data) {
        // There are some events that need specific structs that we can't simply
        // serialize as a string because they might contain null bytes
        switch (opcode) {
            case effOpen:
                // This should not be needed, but some improperly coded plugins
                // such as the Roland Cloud plugins will initialize part of
                // their `AEffect` only after the host calls `effOpen`, instead
                // of during the initialization.
                return WantsAEffectUpdate{};
                break;
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
            case effOpen: {
                // Update our `AEffect` object one last time for improperly
                // coded late initialing plugins. Hopefully the host will see
                // that the object is updated because these plugins don't send
                // any notification about this.
                const auto updated_plugin = std::get<AEffect>(response.payload);
                update_aeffect(plugin, updated_plugin);
            } break;
            case effEditGetRect: {
                // Either the plugin will have returned (a pointer to) their
                // editor dimensions, or they will not have written anything.
                if (std::holds_alternative<std::nullptr_t>(response.payload)) {
                    return;
                }

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
    AEffect& plugin;
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
        logger.log_event(true, opcode, index, value, nullptr, option,
                         std::nullopt);
        logger.log(
            "   WARNING: The host has dispatched an event before the plugin "
            "has finished initializing, ignoring the event. (are we running "
            "Ardour 5.X?)");
        logger.log_event_response(true, opcode, 0, nullptr, std::nullopt);
        return 0;
    }

    DispatchDataConverter converter(chunk_data, plugin, editor_rectangle);

    switch (opcode) {
        case effClose: {
            // Allow the plugin to handle its own shutdown, and then terminate
            // the process. Because terminating the Wine process will also
            // forcefully close all open sockets this will also terminate our
            // handler thread.
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
            vst_host.terminate();

            // The `stop()` method will cause the IO context to just drop all of
            // its work immediately and not throw any exceptions that would have
            // been caused by pipes and sockets being closed.
            io_context.stop();

            // These threads should now be finished because we've forcefully
            // terminated the Wine process, interupting their socket operations
            host_callback_handler.join();
            wine_io_handler.join();

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
                logger.log_event(true, opcode, index, value, query, option,
                                 std::nullopt);
                logger.log(
                    "   The host requests libSwell GUI support which is not "
                    "supported using Wine, ignoring the request.");
                logger.log_event_response(true, opcode, -1, nullptr,
                                          std::nullopt);
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
    const auto response =
        read_object<AudioBuffers>(host_vst_process_replacing, process_buffer);

    assert(response.buffers.size() == static_cast<size_t>(plugin.numOutputs));
    for (int channel = 0; channel < plugin.numOutputs; channel++) {
        std::copy(response.buffers[channel].begin(),
                  response.buffers[channel].end(), outputs[channel]);
    }

    // Plugins are allowed to send MIDI events during processing using a host
    // callback. These have to be processed during the actual
    // `processReplacing()` function or else the host will ignore them. To
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
