// yabridge: a Wine VST bridge
// Copyright (C) 2020-2021 Robbert van der Helm
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

#include "vst2.h"

#include "../../common/communication/vst2.h"
#include "../utils.h"

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
Vst2PluginBridge& get_bridge_instance(const AEffect& plugin) noexcept {
    return *static_cast<Vst2PluginBridge*>(plugin.ptr3);
}

Vst2PluginBridge::Vst2PluginBridge(audioMasterCallback host_callback)
    : PluginBridge(
          PluginType::vst2,
          [](boost::asio::io_context& io_context, const PluginInfo& info) {
              return Vst2Sockets<std::jthread>(
                  io_context,
                  generate_endpoint_base(info.native_library_path.filename()
                                             .replace_extension("")
                                             .string()),
                  true);
          }),
      // All the fields should be zero initialized because
      // `Vst2PluginInstance::vstAudioMasterCallback` from Bitwig's plugin
      // bridge will crash otherwise
      plugin(),
      host_callback_function(host_callback),
      logger(generic_logger) {
    log_init_message();

    // This will block until all sockets have been connected to by the Wine VST
    // host
    connect_sockets_guarded();

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
        set_realtime_priority(true);

        sockets.vst_host_callback.receive_events(
            std::pair<Vst2Logger&, bool>(logger, false),
            [&](Vst2Event& event, bool /*on_main_thread*/) {
                switch (event.opcode) {
                    // MIDI events sent from the plugin back to the host are
                    // a special case here. They have to sent during the
                    // `processReplacing()` function or else the host will
                    // ignore them. Because of this we'll temporarily save
                    // any MIDI events we receive here, and then we'll
                    // actually send them to the host at the end of the
                    // `process_replacing()` function.
                    case audioMasterProcessEvents: {
                        std::lock_guard lock(incoming_midi_events_mutex);

                        incoming_midi_events.push_back(
                            std::get<DynamicVstEvents>(event.payload));

                        return Vst2EventResult{.return_value = 1,
                                               .payload = nullptr,
                                               .value_payload = std::nullopt};
                    } break;
                    // REAPER requires that `audioMasterSizeWindow()` calls are
                    // handled from the GUI thread, which is the thread that
                    // will call `effEditIdle()`. To account for this, we'll
                    // store the last resize request and then only pass it to
                    // the host when it calls `effEditIdle()`.
                    case audioMasterSizeWindow: {
                        std::lock_guard lock(incoming_resize_mutex);

                        incoming_resize = std::pair(event.index, event.value);

                        return Vst2EventResult{.return_value = 1,
                                               .payload = nullptr,
                                               .value_payload = std::nullopt};
                    } break;
                    // HACK: Certain plugins may have undesirable DAW-specific
                    //       behaviour.  Chromaphone 3 for instance has broken
                    //       text input dialogs when using Bitwig. We can work
                    //       around these issues by reporting we're running
                    //       under some other host. We need to do this on the
                    //       plugin side instead of one the Wine side because
                    //       the plugin will likely do this callback during
                    //       initialization, and at that point we will not yet
                    //       have sent the configuration to the plugin.
                    case audioMasterGetProductString: {
                        if (config.hide_daw) {
                            logger.log("The plugin asked for the host's name.");
                            logger.log("Reporting \"" +
                                       std::string(product_name_override) +
                                       "\" instead of the actual host's name.");

                            return Vst2EventResult{
                                .return_value = 1,
                                .payload = product_name_override,
                                .value_payload = std::nullopt};
                        }
                    } break;
                    case audioMasterGetVendorString: {
                        if (config.hide_daw) {
                            logger.log(
                                "The plugin asked for the host's vendor.");
                            logger.log(
                                "Reporting \"" +
                                std::string(vendor_name_override) +
                                "\" instead of the actual host's vendor.");

                            return Vst2EventResult{
                                .return_value = 1,
                                .payload = vendor_name_override,
                                .value_payload = std::nullopt};
                        }
                    } break;
                }

                return passthrough_event(&plugin, host_callback_function,
                                         event);
            });
    });

    // Read the plugin's information from the Wine process. This can only be
    // done after we started accepting host callbacks as the plugin will likely
    // call these during its initialization. Any further updates will be sent
    // over the `dispatcher()` socket. This would happen whenever the plugin
    // calls `audioMasterIOChanged()` and after the host calls `effOpen()`.
    const auto initialization_data =
        sockets.host_vst_control.receive_single<Vst2EventResult>();
    const auto initialized_plugin =
        std::get<AEffect>(initialization_data.payload);

    // After receiving the `AEffect` values we'll want to send the configuration
    // back to complete the startup process
    sockets.host_vst_control.send(config);

    update_aeffect(plugin, initialized_plugin);
}

Vst2PluginBridge::~Vst2PluginBridge() noexcept {
    try {
        // Drop all work make sure all sockets are closed
        plugin_host->terminate();

        // The `stop()` method will cause the IO context to just drop all of its
        // outstanding work immediately
        io_context.stop();
    } catch (const boost::system::system_error&) {
        // It could be that the sockets have already been closed or that the
        // process has already exited (at which point we probably won't be
        // executing this, but maybe if all the stars align)
    }
}

class DispatchDataConverter : public DefaultDataConverter {
   public:
    DispatchDataConverter(std::vector<uint8_t>& chunk_data,
                          AEffect& plugin,
                          VstRect& editor_rectangle) noexcept
        : chunk(chunk_data), plugin(plugin), rect(editor_rectangle) {}

    Vst2Event::Payload read_data(const int opcode,
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
                return ChunkData{
                    std::vector<uint8_t>(chunk_data, chunk_data + value)};
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
            // HACK: REAPER has recently started using `effVendorSpecific` with
            //       a non-pointer `data` argument, so we need to explicitly
            //       handle this
            case effVendorSpecific:
                if (index == effSetSpeakerArrangement) {
                    return static_cast<native_size_t>(
                        reinterpret_cast<size_t>(data));
                } else {
                    return DefaultDataConverter::read_data(opcode, index, value,
                                                           data);
                }
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
            // NOTE: We needed to explicitly handle `audioMasterWantMidi()` on
            //       the Wine side because UVI Plugsound Free would pass garbage
            //       data to `data`, which would of course trigger a segfault
            //       when yabridge would try to read from it. Even though no
            //       Linux hosts do such a thing, we'll do something similar
            //       here just to be consistent.
            case effClose:
            case effSetProgram:
            case effGetProgram:
            case effSetSampleRate:
            case effSetBlockSize:
            case effMainsChanged:
            case effEditClose:
            case effEditIdle:
            case effCanBeAutomated:
            case effGetPlugCategory:
            case effGetVendorVersion:
            case effGetTailSize:
            case effIdle:
            case effGetVstVersion:
            case effBeginSetProgram:
            case effEndSetProgram:
            case effStartProcess:
            case effStopProcess:
                return nullptr;
            default:
                return DefaultDataConverter::read_data(opcode, index, value,
                                                       data);
                break;
        }
    }

    std::optional<Vst2Event::Payload> read_value(
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

    void write_data(const int opcode,
                    void* data,
                    const Vst2EventResult& response) const override {
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
                // `Vst2PluginBridge` and write a pointer to that struct to the
                // data pointer
                const auto buffer =
                    std::get<ChunkData>(response.payload).buffer;
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
                DefaultDataConverter::write_data(opcode, data, response);
                break;
        }
    }

    intptr_t return_value(const int opcode,
                          const intptr_t original) const override {
        return DefaultDataConverter::return_value(opcode, original);
    }

    void write_value(const int opcode,
                     intptr_t value,
                     const Vst2EventResult& response) const override {
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

intptr_t Vst2PluginBridge::dispatch(AEffect* /*plugin*/,
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
                return_value = sockets.host_vst_dispatch.send_event(
                    converter, std::pair<Vst2Logger&, bool>(logger, true),
                    opcode, index, value, data, option);
            } catch (const boost::system::system_error& a) {
                // Thrown when the socket gets closed because the VST plugin
                // loaded into the Wine process crashed during shutdown
                logger.log("The plugin crashed during shutdown, ignoring");
            }

            delete this;

            return return_value;
        }; break;
        case effEditIdle: {
            // This is the only place where we'll deviate from yabridge's
            // 'one-to-one passthrough' philosophy. While in practice we can
            // just pass through `effEditIdle` and we have been doing so until
            // yabridge 3.x, in reality it's much more practical to just run
            // this on a Win32 timer. We would either need to run `effEditIdle`
            // from a non-GUI thread (which could cause issues), or we would
            // need a timer anyways to proc the function when the GUI is being
            // blocked by for instance an open dropdown.
            logger.log_event(true, opcode, index, value, nullptr, option,
                             std::nullopt);

            // REAPER requires `audioMasterSizeWindow()` calls to be done from
            // the GUI thread. In every other host this doesn't make a
            // difference, but in REAPER the FX window only resizes when this is
            // called from here.
            {
                std::unique_lock lock(incoming_resize_mutex);
                if (incoming_resize) {
                    const auto& [width, height] = *incoming_resize;
                    incoming_resize.reset();
                    lock.unlock();

                    host_callback_function(&plugin, audioMasterSizeWindow,
                                           width, height, nullptr, 0.0);
                }
            }

            logger.log_event_response(true, opcode, 0, nullptr, std::nullopt);
            return 0;
        }; break;
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
                    "   The host has requested libSwell GUI support, which is");
                logger.log("   not supported when using Wine.");
                logger.log(
                    "   You can safely ignore this message; this is normal");
                logger.log("   when using REAPER.");
                logger.log("");

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
    return sockets.host_vst_dispatch.send_event(
        converter, std::pair<Vst2Logger&, bool>(logger, true), opcode, index,
        value, data, option);
}

template <typename T, bool replacing>
void Vst2PluginBridge::do_process(T** inputs, T** outputs, int sample_frames) {
    // To prevent unnecessary bridging overhead, we'll send the time information
    // together with the buffers because basically every plugin needs this
    std::optional<VstTimeInfo> current_time_info;
    const VstTimeInfo* returned_time_info =
        reinterpret_cast<const VstTimeInfo*>(host_callback_function(
            &plugin, audioMasterGetTime, 0, 0, nullptr, 0.0));
    if (returned_time_info) {
        current_time_info = *returned_time_info;
    }

    // Some plugisn also ask for the current process level, so we'll prefetch
    // that information as well
    const int current_process_level = static_cast<int>(host_callback_function(
        &plugin, audioMasterGetCurrentProcessLevel, 0, 0, nullptr, 0.0));

    // We'll synchronize the scheduling priority of the audio thread on the Wine
    // plugin host with that of the host's audio thread every once in a while
    std::optional<int> new_realtime_priority;
    const time_t now = time(nullptr);
    if (now > last_audio_thread_priority_synchronization +
                  audio_thread_priority_synchronization_interval) {
        new_realtime_priority = get_realtime_priority();
        last_audio_thread_priority_synchronization = now;
    }

    // The inputs and outputs arrays should be `[num_inputs][sample_frames]` and
    // `[num_outputs][sample_frames]` floats large respectfully.
    std::vector<std::vector<T>> input_buffers(plugin.numInputs,
                                              std::vector<T>(sample_frames));
    for (int channel = 0; channel < plugin.numInputs; channel++) {
        std::copy_n(inputs[channel], sample_frames,
                    input_buffers[channel].begin());
    }

    const AudioBuffers request{.buffers = input_buffers,
                               .sample_frames = sample_frames,
                               .current_time_info = current_time_info,
                               .current_process_level = current_process_level,
                               .new_realtime_priority = new_realtime_priority};
    sockets.host_vst_process_replacing.send(request, process_buffer);

    // Write the results back to the `outputs` arrays
    const auto response =
        sockets.host_vst_process_replacing.receive_single<AudioBuffers>(
            process_buffer);
    const auto& response_buffers =
        std::get<std::vector<std::vector<T>>>(response.buffers);

    assert(response_buffers.size() == static_cast<size_t>(plugin.numOutputs));
    for (int channel = 0; channel < plugin.numOutputs; channel++) {
        if constexpr (replacing) {
            std::copy(response_buffers[channel].begin(),
                      response_buffers[channel].end(), outputs[channel]);
        } else {
            // The old `process()` function expects the plugin to add its output
            // to the accumulated values in `outputs`. Since no host is ever
            // going to call this anyways we won't even bother with a separate
            // implementation and we'll just add `processReplacing()` results to
            // `outputs`.
            // We could use `std::execution::unseq` here but that would require
            // linking to TBB and since this probably won't ever be used anyways
            // that's a bit of a waste.
            std::transform(response_buffers[channel].begin(),
                           response_buffers[channel].end(), outputs[channel],
                           outputs[channel],
                           [](const T& new_value, T& current_value) -> T {
                               return new_value + current_value;
                           });
        }
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

void Vst2PluginBridge::process(AEffect* /*plugin*/,
                               float** inputs,
                               float** outputs,
                               int sample_frames) {
    // Technically either `Vst2PluginBridge::process()` or
    // `Vst2PluginBridge::process_replacing()` could actually call the other
    // function on the plugin depending on what the plugin supports.
    logger.log_trace([]() { return ">> process() :: start"; });
    do_process<float, false>(inputs, outputs, sample_frames);
    logger.log_trace([]() { return "   process() :: end"; });
}

void Vst2PluginBridge::process_replacing(AEffect* /*plugin*/,
                                         float** inputs,
                                         float** outputs,
                                         int sample_frames) {
    logger.log_trace([]() { return ">> processReplacing() :: start"; });
    do_process<float, true>(inputs, outputs, sample_frames);
    logger.log_trace([]() { return "   processReplacing() :: end"; });
}

void Vst2PluginBridge::process_double_replacing(AEffect* /*plugin*/,
                                                double** inputs,
                                                double** outputs,
                                                int sample_frames) {
    logger.log_trace([]() { return ">> processDoubleReplacing() :: start"; });
    do_process<double, true>(inputs, outputs, sample_frames);
    logger.log_trace([]() { return "   processDoubleReplacing() :: end"; });
}

float Vst2PluginBridge::get_parameter(AEffect* /*plugin*/, int index) {
    logger.log_get_parameter(index);

    const Parameter request{index, std::nullopt};
    ParameterResult response;

    // Prevent race conditions from `getParameter()` and `setParameter()` being
    // called at the same time since  they share the same socket
    {
        std::lock_guard lock(parameters_mutex);
        sockets.host_vst_parameters.send(request);

        response =
            sockets.host_vst_parameters.receive_single<ParameterResult>();
    }

    logger.log_get_parameter_response(*response.value);

    return *response.value;
}

void Vst2PluginBridge::set_parameter(AEffect* /*plugin*/,
                                     int index,
                                     float value) {
    logger.log_set_parameter(index, value);

    const Parameter request{index, value};
    ParameterResult response;

    {
        std::lock_guard lock(parameters_mutex);
        sockets.host_vst_parameters.send(request);

        response =
            sockets.host_vst_parameters.receive_single<ParameterResult>();
    }

    logger.log_set_parameter_response();

    // This should not contain any values and just serve as an acknowledgement
    assert(!response.value);
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
    return get_bridge_instance(*plugin).process(plugin, inputs, outputs,
                                                sample_frames);
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
