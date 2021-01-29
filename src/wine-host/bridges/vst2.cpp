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

#include <iostream>
#include <set>

#include "../../common/communication/vst2.h"

/**
 * A function pointer to what should be the entry point of a VST plugin.
 */
using VstEntryPoint = AEffect*(VST_CALL_CONV*)(audioMasterCallback);

/**
 * This ugly global is needed so we can get the instance of a `Vst2Bridge` class
 * from an `AEffect` when it performs a host callback during its initialization.
 */
Vst2Bridge* current_bridge_instance = nullptr;

/**
 * Opcodes that should always be handled on the main thread because they may
 * involve GUI operations.
 *
 * NOTE: `effMainsChanged` is the odd one here. EZdrummer interacts with the
 *       Win32 message loop while handling this function. If we don't execute
 *       this from the main GUI thread, then EZdrummer won't produce any sound.
 */
const std::set<int> unsafe_opcodes{effOpen,     effClose,       effEditGetRect,
                                   effEditOpen, effEditClose,   effEditIdle,
                                   effEditTop,  effMainsChanged};

intptr_t VST_CALL_CONV
host_callback_proxy(AEffect*, int, int, intptr_t, void*, float);

/**
 * Fetch the Vst2Bridge instance stored in one of the two pointers reserved
 * for the host of the hosted VST plugin. This is sadly needed as a workaround
 * to avoid using globals since we need free function pointers to interface with
 * the VST C API.
 */
Vst2Bridge& get_bridge_instance(const AEffect* plugin) {
    // This is needed during the initialization of the plugin since we can only
    // add our own pointer after it's done initializing
    if (current_bridge_instance) {
        return *current_bridge_instance;
    }

    return *static_cast<Vst2Bridge*>(plugin->ptr1);
}

Vst2Bridge::Vst2Bridge(MainContext& main_context,
                       std::string plugin_dll_path,
                       std::string endpoint_base_dir)
    : HostBridge(plugin_dll_path),
      main_context(main_context),
      plugin_handle(LoadLibrary(plugin_dll_path.c_str()), FreeLibrary),
      sockets(main_context.context, endpoint_base_dir, false) {
    // Got to love these C APIs
    if (!plugin_handle) {
        throw std::runtime_error("Could not load the Windows .dll file at '" +
                                 plugin_dll_path + "'");
    }

    // VST plugin entry point functions should be called `VSTPluginMain`, but
    // there are some older deprecated names that legacy plugins may still use
    VstEntryPoint vst_entry_point = nullptr;
    for (auto name : {"VSTPluginMain", "main_plugin", "main"}) {
        vst_entry_point =
            reinterpret_cast<VstEntryPoint>(reinterpret_cast<size_t>(
                GetProcAddress(plugin_handle.get(), name)));

        if (vst_entry_point) {
            break;
        }
    }
    if (!vst_entry_point) {
        throw std::runtime_error(
            "Could not find a valid VST entry point for '" + plugin_dll_path +
            "'.");
    }

    sockets.connect();

    // Initialize after communication has been set up We'll try to do the same
    // `get_bridge_instance` trick as in `plugin/bridges/vst2.cpp`, but since
    // the plugin will probably call the host callback while it's initializing
    // we sadly have to use a global here.
    current_bridge_instance = this;
    plugin = vst_entry_point(host_callback_proxy);
    if (!plugin) {
        throw std::runtime_error("VST plugin at '" + plugin_dll_path +
                                 "' failed to initialize.");
    }

    // We only needed this little hack during initialization
    current_bridge_instance = nullptr;
    plugin->ptr1 = this;

    // Send the plugin's information to the Linux VST plugin. Any other updates
    // of this object will be sent over the `dispatcher()` socket. This would be
    // done after the host calls `effOpen()`, and when the plugin calls
    // `audioMasterIOChanged()`.
    sockets.host_vst_control.send(EventResult{
        .return_value = 0, .payload = *plugin, .value_payload = std::nullopt});

    // After sending the AEffect struct we'll receive this instance's
    // configuration as a response
    config = sockets.host_vst_control.receive_single<Configuration>();

    // Allow this plugin to configure the main context's tick rate
    main_context.update_timer_interval(config.event_loop_interval());

    parameters_handler = Win32Thread([&]() {
        sockets.host_vst_parameters.receive_multi<Parameter>(
            [&](Parameter request, std::vector<uint8_t>& buffer) {
                // Both `getParameter` and `setParameter` functions are passed
                // through on this socket since they have a lot of overlap. The
                // presence of the `value` field tells us which one we're
                // dealing with.
                if (request.value) {
                    // `setParameter`
                    plugin->setParameter(plugin, request.index, *request.value);

                    ParameterResult response{std::nullopt};
                    sockets.host_vst_parameters.send(response, buffer);
                } else {
                    // `getParameter`
                    float value = plugin->getParameter(plugin, request.index);

                    ParameterResult response{value};
                    sockets.host_vst_parameters.send(response, buffer);
                }
            });
    });

    process_replacing_handler = Win32Thread([&]() {
        // These are used as scratch buffers to prevent unnecessary allocations.
        // Since don't know in advance whether the host will call
        // `processReplacing` or `processDoubleReplacing` we'll just create
        // both.
        std::vector<std::vector<float>> output_buffers_single_precision(
            plugin->numOutputs);
        std::vector<std::vector<double>> output_buffers_double_precision(
            plugin->numOutputs);

        sockets.host_vst_process_replacing.receive_multi<AudioBuffers>(
            [&](AudioBuffers request, std::vector<uint8_t>& buffer) {
                // As suggested by Jack Winter, we'll synchronize this thread's
                // audio processing priority with that of the host's audio
                // thread every once in a while
                if (request.new_realtime_priority) {
                    set_realtime_priority(true, *request.new_realtime_priority);
                }

                // Let the plugin process the MIDI events that were received
                // since the last buffer, and then clean up those events. This
                // approach should not be needed but Kontakt only stores
                // pointers to rather than copies of the events.
                std::lock_guard lock(next_buffer_midi_events_mutex);

                // HACK: Workaround for a bug in SWAM Cello where it would call
                //       `audioMasterGetTime()` once for every sample. The first
                //       value returned by this function during an audio
                //       processing cycle will be reused for the rest of the
                //       cycle.
                cached_time_info.reset();

                // Since the host should only be calling one of `process()`,
                // processReplacing()` or `processDoubleReplacing()`, we can all
                // handle them over the same socket. We pick which one to call
                // depending on the type of data we got sent and the plugin's
                // reported support for these functions.
                std::visit(
                    overload{
                        [&](std::vector<std::vector<float>>& input_buffers) {
                            // The process functions expect a `float**` for
                            // their inputs and their outputs
                            std::vector<float*> inputs;
                            for (auto& buffer : input_buffers) {
                                inputs.push_back(buffer.data());
                            }

                            // We reuse the buffers to avoid some unnecessary
                            // heap allocations, so we need to make sure the
                            // buffers are large enough since plugins can change
                            // their output configuration. The type we're using
                            // here (single precision floats vs double
                            // precisioon doubles) should be the same as the one
                            // we're sending in our response.
                            std::vector<float*> outputs;
                            output_buffers_single_precision.resize(
                                plugin->numOutputs);
                            for (auto& buffer :
                                 output_buffers_single_precision) {
                                buffer.resize(request.sample_frames);
                                outputs.push_back(buffer.data());
                            }

                            // Any plugin made in the last fifteen years or so
                            // should support `processReplacing`. In the off
                            // chance it does not we can just emulate this
                            // behavior ourselves.
                            if (plugin->processReplacing) {
                                plugin->processReplacing(plugin, inputs.data(),
                                                         outputs.data(),
                                                         request.sample_frames);
                            } else {
                                // If we zero out this buffer then the behavior
                                // is the same as `processReplacing``
                                for (std::vector<float>& buffer :
                                     output_buffers_single_precision) {
                                    std::fill(buffer.begin(), buffer.end(),
                                              0.0);
                                }

                                plugin->process(plugin, inputs.data(),
                                                outputs.data(),
                                                request.sample_frames);
                            }

                            AudioBuffers response{
                                .buffers = output_buffers_single_precision,
                                .sample_frames = request.sample_frames,
                                .new_realtime_priority = std::nullopt};
                            sockets.host_vst_process_replacing.send(response,
                                                                    buffer);
                        },
                        [&](std::vector<std::vector<double>>& input_buffers) {
                            // Exactly the same as the above, but for double
                            // precision audio
                            std::vector<double*> inputs;
                            for (auto& buffer : input_buffers) {
                                inputs.push_back(buffer.data());
                            }

                            std::vector<double*> outputs;
                            output_buffers_double_precision.resize(
                                plugin->numOutputs);
                            for (auto& buffer :
                                 output_buffers_double_precision) {
                                buffer.resize(request.sample_frames);
                                outputs.push_back(buffer.data());
                            }

                            plugin->processDoubleReplacing(
                                plugin, inputs.data(), outputs.data(),
                                request.sample_frames);

                            AudioBuffers response{
                                .buffers = output_buffers_double_precision,
                                .sample_frames = request.sample_frames,
                                .new_realtime_priority = std::nullopt};
                            sockets.host_vst_process_replacing.send(response,
                                                                    buffer);
                        }},
                    request.buffers);

                next_audio_buffer_midi_events.clear();
            });
    });
}

bool Vst2Bridge::inhibits_event_loop() {
    return !is_initialized;
}

void Vst2Bridge::run() {
    sockets.host_vst_dispatch.receive_events(
        std::nullopt, [&](Event& event, bool /*on_main_thread*/) {
            if (event.opcode == effProcessEvents) {
                // For 99% of the plugins we can just call
                // `effProcessReplacing()` and be done with it, but a select few
                // plugins (I could only find Kontakt that does this) don't
                // actually make copies of the events they receive and only
                // store pointers to those events, meaning that they have to
                // live at least until the next audio buffer gets processed.
                // We're not using `passthrough_events()` here directly because
                // we need to store a copy of the `DynamicVstEvents` struct
                // before passing the generated `VstEvents` object to the
                // plugin.
                std::lock_guard lock(next_buffer_midi_events_mutex);

                next_audio_buffer_midi_events.push_back(
                    std::get<DynamicVstEvents>(event.payload));
                DynamicVstEvents& events = next_audio_buffer_midi_events.back();

                // Exact same handling as in `passthrough_event()`, apart
                // from making a copy of the events first
                const intptr_t return_value = plugin->dispatcher(
                    plugin, event.opcode, event.index, event.value,
                    &events.as_c_events(), event.option);

                EventResult response{.return_value = return_value,
                                     .payload = nullptr,
                                     .value_payload = std::nullopt};

                return response;
            } else {
                return passthrough_event(
                    plugin,
                    [&](AEffect* plugin, int opcode, int index, intptr_t value,
                        void* data, float option) -> intptr_t {
                        // HACK: Ardour 6.3 will call `effEditIdle` before
                        //       `effEditOpen`, which causes some plugins to
                        //       crash. This has been fixed as of
                        //       https://github.com/Ardour/ardour/commit/f7cb1b0b481eeda755bdf8eb9fc5f90a81d2aa01.
                        //       We should keep this in until Ardour 6.3 is no
                        //       longer in distro repositories.
                        //
                        //       Note that now that we run `effEditIdle`
                        //       entirely off of a Win32 timer this will never
                        //       get hit, but we'll keep it in for the sake of
                        //       preserving correct behaviour.
                        if (opcode == effEditIdle && !editor) {
                            std::cerr << "WARNING: The host is calling "
                                         "`effEditIdle()` while the "
                                         "plugin's editor is closed, "
                                         "filtering the request (is "
                                         "this Ardour?). This bug should "
                                         "be reported to the host."
                                      << std::endl;
                            return 0;
                        }

                        // Certain functions will most definitely involve the
                        // GUI or the Win32 message loop. These functions have
                        // to be performed on the thread that is running the IO
                        // context, since this is also where the plugins were
                        // instantiated and where the Win32 message loop is
                        // handled.
                        if (unsafe_opcodes.contains(opcode)) {
                            return main_context
                                .run_in_context<intptr_t>([&]() {
                                    const intptr_t result =
                                        dispatch_wrapper(plugin, opcode, index,
                                                         value, data, option);

                                    // The Win32 message loop will not be run up
                                    // to this point to prevent plugins with
                                    // partially initialized states from
                                    // misbehaving
                                    if (opcode == effOpen) {
                                        is_initialized = true;
                                    }

                                    return result;
                                })
                                .get();
                        } else {
                            return dispatch_wrapper(plugin, opcode, index,
                                                    value, data, option);
                        }
                    },
                    event);
            }
        });
}

void Vst2Bridge::handle_x11_events() {
    if (editor) {
        editor->handle_x11_events();
    }
}

void Vst2Bridge::handle_win32_events() {
    MSG msg;

    for (int i = 0;
         i < max_win32_messages && PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE);
         i++) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

intptr_t Vst2Bridge::dispatch_wrapper(AEffect* plugin,
                                      int opcode,
                                      int index,
                                      intptr_t value,
                                      void* data,
                                      float option) {
    // We have to intercept GUI open calls since we can't use
    // the X11 window handle passed by the host
    switch (opcode) {
        case effEditOpen: {
            // Create a Win32 window through Wine, embed it into the window
            // provided by the host, and let the plugin embed itself into
            // the Wine window
            const auto x11_handle = reinterpret_cast<size_t>(data);

            // NOTE: Just like in the event loop, we want to run this with lower
            //       priority to prevent whatever operation the plugin does
            //       while it's loading its editor from preempting the audio
            //       thread.
            set_realtime_priority(false);
            Editor& editor_instance = editor.emplace(
                main_context, config, x11_handle, [plugin = this->plugin]() {
                    plugin->dispatcher(plugin, effEditIdle, 0, 0, nullptr, 0.0);
                });
            const intptr_t result =
                plugin->dispatcher(plugin, opcode, index, value,
                                   editor_instance.get_win32_handle(), option);
            set_realtime_priority(true);

            return result;
        } break;
        case effEditClose: {
            // Cleanup is handled through RAII
            set_realtime_priority(false);
            const intptr_t return_value =
                plugin->dispatcher(plugin, opcode, index, value, data, option);
            editor.reset();
            set_realtime_priority(true);

            return return_value;
        } break;
        case effEditGetRect: {
            set_realtime_priority(false);
            const intptr_t return_value =
                plugin->dispatcher(plugin, opcode, index, value, data, option);
            set_realtime_priority(true);

            return return_value;
        } break;
        default:
            return plugin->dispatcher(plugin, opcode, index, value, data,
                                      option);
            break;
    }
}

class HostCallbackDataConverter : DefaultDataConverter {
   public:
    HostCallbackDataConverter(AEffect* plugin,
                              std::optional<VstTimeInfo>& time_info)
        : plugin(plugin), time_info(time_info) {}

    EventPayload read(const int opcode,
                      const int index,
                      const intptr_t value,
                      const void* data) const override {
        switch (opcode) {
            case audioMasterGetTime:
                return WantsVstTimeInfo{};
                break;
            case audioMasterIOChanged:
                // This is a helpful event that indicates that the VST
                // plugin's `AEffect` struct has changed. Writing these
                // results back is done inside of `passthrough_event()`.
                return AEffect(*plugin);
                break;
            case audioMasterProcessEvents:
                return DynamicVstEvents(*static_cast<const VstEvents*>(data));
                break;
            // We detect whether an opcode should return a string by
            // checking whether there's a zeroed out buffer behind the void
            // pointer. This works for any host, but not all plugins zero
            // out their buffers.
            case audioMasterGetVendorString:
            case audioMasterGetProductString:
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
        return DefaultDataConverter::read_value(opcode, value);
    }

    void write(const int opcode,
               void* data,
               const EventResult& response) const override {
        switch (opcode) {
            case audioMasterGetTime:
                // Write the returned `VstTimeInfo` struct into a field and
                // make the function return a pointer to it in the function
                // below. Depending on whether the host supported the
                // requested time information this operations returns either
                // a null pointer or a pointer to a `VstTimeInfo` object.
                if (std::holds_alternative<std::nullptr_t>(response.payload)) {
                    time_info = std::nullopt;
                } else {
                    time_info = std::get<VstTimeInfo>(response.payload);
                }
                break;
            default:
                DefaultDataConverter::write(opcode, data, response);
                break;
        }
    }

    intptr_t return_value(const int opcode,
                          const intptr_t original) const override {
        switch (opcode) {
            case audioMasterGetTime: {
                // Return a pointer to the `VstTimeInfo` object written in
                // the function above
                VstTimeInfo* time_info_pointer = nullptr;
                if (time_info) {
                    time_info_pointer = &*time_info;
                }

                return reinterpret_cast<intptr_t>(time_info_pointer);
            } break;
            default:
                return DefaultDataConverter::return_value(opcode, original);
                break;
        }
    }

    void write_value(const int opcode,
                     intptr_t value,
                     const EventResult& response) const override {
        return DefaultDataConverter::write_value(opcode, value, response);
    }

   private:
    AEffect* plugin;
    std::optional<VstTimeInfo>& time_info;
};

intptr_t Vst2Bridge::host_callback(AEffect* effect,
                                   int opcode,
                                   int index,
                                   intptr_t value,
                                   void* data,
                                   float option) {
    // HACK: Workaround for a bug in SWAM Cello where it would call
    //       `audioMasterGetTime()` once for every sample. The `time_info` value
    //       is assigned inside of `HostCallbackDataConverter::write()`. At the
    //       beginning of the processing cycle this value will be reset.
    if (cached_time_info) {
        return reinterpret_cast<intptr_t>(&*cached_time_info);
    }

    HostCallbackDataConverter converter(effect, cached_time_info);
    return sockets.vst_host_callback.send_event(converter, std::nullopt, opcode,
                                                index, value, data, option);
}

intptr_t VST_CALL_CONV host_callback_proxy(AEffect* effect,
                                           int opcode,
                                           int index,
                                           intptr_t value,
                                           void* data,
                                           float option) {
    return get_bridge_instance(effect).host_callback(effect, opcode, index,
                                                     value, data, option);
}
