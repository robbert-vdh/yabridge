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

// Generated inside of the build directory
#include <src/common/config/config.h>
#include <src/common/config/version.h>

#include "../common/communication.h"
#include "../common/events.h"
#include "../common/utils.h"
#include "utils.h"

namespace bp = boost::process;
// I'd rather use std::filesystem instead, but Boost.Process depends on
// boost::filesystem
namespace fs = boost::filesystem;

intptr_t dispatch_proxy(AEffect*, int, int, intptr_t, void*, float);
void process_proxy(AEffect*, float**, float**, int);
void process_replacing_proxy(AEffect*, float**, float**, int);
void process_double_replacing_proxy(AEffect*, double**, double**, int);
void set_parameter_proxy(AEffect*, int, float);
float get_parameter_proxy(AEffect*, int);

/**
 * Fetch the bridge instance stored in an unused pointer from a VST plugin. This
 * is sadly needed as a workaround to avoid using globals since we need free
 * function pointers to interface with the VST C API.
 */
PluginBridge& get_bridge_instance(const AEffect& plugin) {
    return *static_cast<PluginBridge*>(plugin.ptr3);
}

PluginBridge::PluginBridge(audioMasterCallback host_callback)
    : config(load_config_for(get_this_file_location())),
      vst_plugin_path(find_vst_plugin()),
      // All the fields should be zero initialized because
      // `Vst2PluginInstance::vstAudioMasterCallback` from Bitwig's plugin
      // bridge will crash otherwise
      plugin(),
      io_context(),
      socket_endpoint(generate_plugin_endpoint().string()),
      socket_acceptor(io_context, socket_endpoint),
      host_vst_dispatch(io_context),
      host_vst_dispatch_midi_events(io_context),
      vst_host_callback(io_context),
      host_vst_parameters(io_context),
      host_vst_process_replacing(io_context),
      host_vst_control(io_context),
      host_callback_function(host_callback),
      logger(Logger::create_from_environment(
          create_logger_prefix(socket_endpoint.path()))),
      wine_version(get_wine_version()),
      vst_host(
          config.group
              ? std::unique_ptr<HostProcess>(
                    std::make_unique<GroupHost>(io_context,
                                                logger,
                                                vst_plugin_path,
                                                socket_endpoint.path(),
                                                *config.group,
                                                host_vst_dispatch))
              : std::unique_ptr<HostProcess>(
                    std::make_unique<IndividualHost>(io_context,
                                                     logger,
                                                     vst_plugin_path,
                                                     socket_endpoint.path()))),
      has_realtime_priority(set_realtime_priority()),
      wine_io_handler([&]() { io_context.run(); }) {
    log_init_message();

#ifndef WITH_WINEDBG
    // If the Wine process fails to start, then nothing will connect to the
    // sockets and we'll be hanging here indefinitely. To prevent this, we'll
    // periodically poll whether the Wine process is still running, and throw
    // when it is not. The alternative would be to rewrite this to using
    // `async_accept`, Boost.Asio timers, and another IO context, but I feel
    // like this a much simpler solution.
    host_guard_handler = std::jthread([&](std::stop_token st) {
        using namespace std::literals::chrono_literals;

        while (!st.stop_requested()) {
            if (!vst_host->running()) {
                logger.log(
                    "The Wine host process has exited unexpectedly. Check the "
                    "output above for more information.");
                std::terminate();
            }

            std::this_thread::sleep_for(20ms);
        }
    });
#endif

    // It's very important that these sockets are connected to in the same
    // order in the Wine VST host
    socket_acceptor.accept(host_vst_dispatch);
    socket_acceptor.accept(host_vst_dispatch_midi_events);
    socket_acceptor.accept(vst_host_callback);
    socket_acceptor.accept(host_vst_parameters);
    socket_acceptor.accept(host_vst_process_replacing);
    socket_acceptor.accept(host_vst_control);

#ifndef WITH_WINEDBG
    host_guard_handler.request_stop();
#endif

    // There's no need to keep the socket endpoint file around after accepting
    // all the sockets, and RAII won't clean these files up for us
    socket_acceptor.close();
    fs::remove(socket_endpoint.path());

    // Set up all pointers for our `AEffect` struct. We will fill this with data
    // from the VST plugin loaded in Wine at the end of this constructor.
    plugin.ptr3 = this;
    plugin.dispatcher = dispatch_proxy;
    plugin.process = process_proxy;
    plugin.setParameter = set_parameter_proxy;
    plugin.getParameter = get_parameter_proxy;
    plugin.processReplacing = process_replacing_proxy;
    plugin.processDoubleReplacing = process_double_replacing_proxy;

    // For our communication we use simple threads and blocking operations
    // instead of asynchronous IO since communication has to be handled in
    // lockstep anyway
    host_callback_handler = std::jthread([&]() {
        while (true) {
            try {
                // TODO: Think of a nicer way to structure this and the similar
                //       handler in `Vst2Bridge::handle_dispatch_midi_events`
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
                            EventResult response{.return_value = 1,
                                                 .payload = nullptr,
                                                 .value_payload = std::nullopt};

                            return response;
                        } else {
                            return passthrough_event(
                                &plugin, host_callback_function)(event);
                        }
                    });
            } catch (const boost::system::system_error&) {
                // This happens when the sockets got closed because the plugin
                // is being shut down
                break;
            }
        }
    });

    // Read the plugin's information from the Wine process. This can only be
    // done after we started accepting host callbacks as the plugin will likely
    // call these during its initialization. Any further updates will be sent
    // over the `dispatcher()` socket. This would happen whenever the plugin
    // calls `audioMasterIOChanged()` and after the host calls `effOpen()`.
    const auto initialization_data = read_object<EventResult>(host_vst_control);
    const auto initialized_plugin =
        std::get<AEffect>(initialization_data.payload);

    // After receiving the `AEffect` values we'll want to send the configuration
    // back to complete the startup process
    write_object(host_vst_control, config);

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
                      const void* data) const override {
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

    std::optional<EventPayload> read_value(
        const int opcode,
        const intptr_t value) const override {
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

    void write(const int opcode,
               void* data,
               const EventResult& response) const override {
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

    intptr_t return_value(const int opcode,
                          const intptr_t original) const override {
        return DefaultDataConverter::return_value(opcode, original);
    }

    void write_value(const int opcode,
                     intptr_t value,
                     const EventResult& response) const override {
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
            "   Warning: The host has dispatched an event before the plugin "
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

            vst_host->terminate();

            // The `stop()` method will cause the IO context to just drop all of
            // its work immediately and not throw any exceptions that would have
            // been caused by pipes and sockets being closed.
            io_context.stop();

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

                logger.log("");
                logger.log(
                    "   The host has requested libSwell GUI support which is ");
                logger.log(
                    "   not supported when using Wine, ignoring the request.");
                logger.log(
                    "   You can safely ignore this message. This is normal");
                logger.log("   when using REAPER.");
                logger.log("");

                // Since the user is using REAPER, also show a reminder that the
                // REAPER workaround should be enabled when it is not yet
                // enabled since it may be easy to miss
                if (!config.hack_reaper_update_display) {
                    logger.log(
                        "   With using REAPER you will have to enable the");
                    logger.log(
                        "   'hack_reaper_update_display' option to prevent");
                    logger.log(
                        "   certain plugins from crashing. To do so, create a");
                    logger.log(
                        "   new file named 'yabridge.toml' next to your");
                    logger.log("   plugins with the following contents:");
                    logger.log("");
                    logger.log(
                        "   # "
                        "https://github.com/robbert-vdh/"
                        "yabridge#runtime-dependencies-and-known-issues");
                    logger.log("   [\"*\"]");
                    logger.log("   hack_reaper_update_display = true");
                    logger.log("");
                }

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

template <typename T>
void PluginBridge::do_process(T** inputs, T** outputs, int sample_frames) {
    // The inputs and outputs arrays should be `[num_inputs][sample_frames]` and
    // `[num_outputs][sample_frames]` floats large respectfully.
    std::vector<std::vector<T>> input_buffers(plugin.numInputs,
                                              std::vector<T>(sample_frames));
    for (int channel = 0; channel < plugin.numInputs; channel++) {
        std::copy(inputs[channel], inputs[channel] + sample_frames,
                  input_buffers[channel].begin());
    }

    const AudioBuffers request{input_buffers, sample_frames};
    write_object(host_vst_process_replacing, request, process_buffer);

    // Write the results back to the `outputs` arrays
    const auto response =
        read_object<AudioBuffers>(host_vst_process_replacing, process_buffer);
    const auto& response_buffers =
        std::get<std::vector<std::vector<T>>>(response.buffers);

    assert(response_buffers.size() == static_cast<size_t>(plugin.numOutputs));
    for (int channel = 0; channel < plugin.numOutputs; channel++) {
        std::copy(response_buffers[channel].begin(),
                  response_buffers[channel].end(), outputs[channel]);
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

void PluginBridge::process_replacing(AEffect* /*plugin*/,
                                     float** inputs,
                                     float** outputs,
                                     int sample_frames) {
    do_process<float>(inputs, outputs, sample_frames);
}

void PluginBridge::process_double_replacing(AEffect* /*plugin*/,
                                            double** inputs,
                                            double** outputs,
                                            int sample_frames) {
    do_process<double>(inputs, outputs, sample_frames);
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

    logger.log_get_parameter_response(*response.value);

    return *response.value;
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
    assert(!response.value);
}

void PluginBridge::log_init_message() {
    std::stringstream init_msg;

    init_msg << "Initializing yabridge version " << yabridge_git_version
             << std::endl;
    init_msg << "host:         '" << vst_host->path().string() << "'"
             << std::endl;
    init_msg << "plugin:       '" << vst_plugin_path.string() << "'"
             << std::endl;
    init_msg << "realtime:     '" << (has_realtime_priority ? "yes" : "no")
             << "'" << std::endl;
    init_msg << "socket:       '" << socket_endpoint.path() << "'" << std::endl;
    init_msg << "wine prefix:  '";

    // If the Wine prefix is manually overridden, then this should be made
    // clear. This follows the behaviour of `set_wineprefix()`.
    bp::environment env = boost::this_process::environment();
    if (!env["WINEPREFIX"].empty()) {
        init_msg << env["WINEPREFIX"].to_string() << " <overridden>";
    } else {
        init_msg << find_wineprefix().value_or("<default>").string();
    }
    init_msg << "'" << std::endl;

    init_msg << "wine version: '" << wine_version << "'" << std::endl;
    init_msg << std::endl;

    // Print the path to the currently loaded configuration file and all
    // settings in use. Printing the matched glob pattern could also be useful
    // but it'll be very noisy and it's likely going to be clear from the shown
    // values anyways.
    init_msg << "config from:   '"
             << config.matched_file.value_or("<defaults>").string() << "'"
             << std::endl;

    init_msg << "hosting mode:  '";
    if (config.group) {
        init_msg << "plugin group \"" << *config.group << "\"";
    } else {
        init_msg << "individually";
    }
    if (vst_host->architecture() == PluginArchitecture::vst_32) {
        init_msg << ", 32-bit";
    } else {
        init_msg << ", 64-bit";
    }
    init_msg << "'" << std::endl;

    bool other_options_set = false;
    init_msg << "other options: '";
    if (config.editor_double_embed) {
        init_msg << "editor: double embed";
        other_options_set = true;
    }
    if (config.hack_reaper_update_display) {
        init_msg << "hack: REAPER 'audioMasterUpdateDisplay' workaround";
        other_options_set = true;
    }
    if (!other_options_set) {
        init_msg << "<none>";
    }
    init_msg << "'" << std::endl;
    init_msg << std::endl;

    // Include a list of enabled compile-tiem features, mostly to make debug
    // logs more useful
    init_msg << "Enabled features:" << std::endl;
#ifdef WITH_BITBRIDGE
    init_msg << "- bitbridge support" << std::endl;
#endif
#ifdef WITH_WINEDBG
    init_msg << "- winedbg" << std::endl;
#endif
#if !(defined(WITH_BITBRIDGE) || defined(WITH_WINEDBG))
    init_msg << "  <none>" << std::endl;
#endif
    init_msg << std::endl;

    for (std::string line = ""; std::getline(init_msg, line);) {
        logger.log(line);
    }
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
    // FIXME: This is incorrect, and I only noticed just now. I'm 99% sure no
    //        hosts actually use this, but this will overwrite the buffer. On
    //        the plugin side we do properly handle plugins that only support
    //        the old cumulative process function.
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

void process_double_replacing_proxy(AEffect* plugin,
                                    double** inputs,
                                    double** outputs,
                                    int sample_frames) {
    return get_bridge_instance(*plugin).process_double_replacing(
        plugin, inputs, outputs, sample_frames);
}

void set_parameter_proxy(AEffect* plugin, int index, float value) {
    return get_bridge_instance(*plugin).set_parameter(plugin, index, value);
}

float get_parameter_proxy(AEffect* plugin, int index) {
    return get_bridge_instance(*plugin).get_parameter(plugin, index);
}
