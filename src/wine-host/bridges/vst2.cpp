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
 * Callbacks (presumably made from the GUI thread) that may receive responses
 * that have to be handled from the same thread. If we don't do this, then those
 * respones might either cause a deadlock when the plugin uses recursive
 * mutexes, or it may result in some other thread safety issues.
 *
 * NOTE: This is needed for Voxengo VST2 plugins in Renoise. When
 *       `effSetChunk()` is called from the GUI thread, Voxengo VST2 plugins
 *       will (wrongly) call `audioMasterUpdateDisplay()` while handling that
 *       call. Renoise then calls `effGetProgram()` while handling that which
 *       shouldn't cause any issues, but the Voxengo plugins try to lock
 *       recursive mutexes on both functions so `effGetProgram()` _has_ to be
 *       called on the same thread that is currently calling
 *       `audioMasterUpdateDisplay()`.
 */
static const std::set<int> mutually_recursive_callbacks{
    audioMasterUpdateDisplay};

/**
 * Opcodes that, when called on this plugin's dispatcher, have to be handled
 * mutually recursively, if possible. This means that the plugin makes a
 * callback using one of the functions defined in
 * `mutually_recursive_callbacks`, and when the host responds by calling one of
 * these functions, then that function should be handled on the same thread
 * where the plugin originally called the request on. If no mutually recursive
 * calling sequence is active while one of these functions is called, then we'll
 * just execute the function directly on the calling thread. See above for a
 * list of situations where this may be necessary.
 */
static const std::set<int> safe_mutually_recursive_requests{effGetProgram};

/**
 * Opcodes that should always be handled on the main thread because they may
 * involve GUI operations.
 *
 * NOTE: `effMainsChanged` is the odd one here. EZdrummer interacts with the
 *       Win32 message loop while handling this function. If we don't execute
 *       this from the main GUI thread, then EZdrummer won't produce any sound.
 * NOTE: `effSetChunk` and `effGetChunk` should be callable from any thread, but
 *       Algonaut Atlas doesn't restore chunk data unless `effSetChunk` is run
 *       from the GUI thread
 */
static const std::set<int> unsafe_requests{
    effOpen,     effClose,   effEditGetRect,  effEditOpen, effEditClose,
    effEditIdle, effEditTop, effMainsChanged, effGetChunk, effSetChunk};

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
                       std::string endpoint_base_dir,
                       pid_t parent_pid)
    : HostBridge(main_context, plugin_dll_path, parent_pid),
      logger(generic_logger),
      plugin_handle(LoadLibrary(plugin_dll_path.c_str()), FreeLibrary),
      sockets(main_context.context, endpoint_base_dir, false) {
    // HACK: If the plugin library was unable to load, then there's a tiny
    //       chance that the plugin expected the COM library to already be
    //       initialized. I've only seen PSPaudioware's InfiniStrip do this. In
    //       that case, we'll initialize the COM library for them and try again.
    if (!plugin_handle) {
        OleInitialize(nullptr);
        plugin_handle.reset(LoadLibrary(plugin_dll_path.c_str()));
        if (plugin_handle) {
            std::cerr << "WARNING: '" << plugin_dll_path << "'" << std::endl;
            std::cerr << "         could only load after we manually"
                      << std::endl;
            std::cerr << "         initialized the COM library." << std::endl;
        }
    }

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
    // Note that this reinterpret cast is not needed at all since the function
    // pointer types are exactly the same, but clangd will complain otherwise
    current_bridge_instance = this;
    plugin = vst_entry_point(
        reinterpret_cast<audioMasterCallback>(host_callback_proxy));
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
    sockets.host_vst_control.send(Vst2EventResult{
        .return_value = 0, .payload = *plugin, .value_payload = std::nullopt});

    // After sending the AEffect struct we'll receive this instance's
    // configuration as a response
    config = sockets.host_vst_control.receive_single<Configuration>();

    // Allow this plugin to configure the main context's tick rate
    main_context.update_timer_interval(config.event_loop_interval());

    parameters_handler = Win32Thread([&]() {
        sockets.host_vst_parameters.receive_multi<Parameter>(
            [&](Parameter& request, SerializationBufferBase& buffer) {
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
        // Most plugins will already enable FTZ, but there are a handful of
        // plugins that don't that suffer from extreme DSP load increases when
        // they start producing denormals
        ScopedFlushToZero ftz_guard;

        sockets.host_vst_process_replacing.receive_multi<
            AudioBuffers>([&](AudioBuffers& process_request,
                              SerializationBufferBase& buffer) {
            // Since the value cannot change during this processing cycle, we'll
            // send the current transport information as part of the request so
            // we prefetch it to avoid unnecessary callbacks from the audio
            // thread
            std::optional<decltype(time_info_cache)::Guard>
                time_info_cache_guard =
                    process_request.current_time_info
                        ? std::optional(time_info_cache.set(
                              *process_request.current_time_info))
                        : std::nullopt;

            // We'll also prefetch the process level, since some plugins will
            // ask for this during every processing cycle
            decltype(process_level_cache)::Guard process_level_cache_guard =
                process_level_cache.set(process_request.current_process_level);

            // As suggested by Jack Winter, we'll synchronize this thread's
            // audio processing priority with that of the host's audio thread
            // every once in a while
            if (process_request.new_realtime_priority) {
                set_realtime_priority(true,
                                      *process_request.new_realtime_priority);
            }

            // Let the plugin process the MIDI events that were received
            // since the last buffer, and then clean up those events. This
            // approach should not be needed but Kontakt only stores
            // pointers to rather than copies of the events.
            std::lock_guard lock(next_buffer_midi_events_mutex);

            // Since the host should only be calling one of `process()`,
            // processReplacing()` or `processDoubleReplacing()`, we can all
            // handle them over the same socket. We pick which one to call
            // depending on the type of data we got sent and the plugin's
            // reported support for these functions.
            std::visit(
                [&]<typename T>(
                    std::vector<std::vector<T>>& input_audio_buffers) {
                    // The process functions expect a `T**` for their inputs
                    thread_local std::vector<T*> input_pointers{};
                    if (input_pointers.size() != input_audio_buffers.size()) {
                        input_pointers.resize(input_audio_buffers.size());
                        for (size_t channel = 0;
                             channel < input_audio_buffers.size(); channel++) {
                            input_pointers[channel] =
                                input_audio_buffers[channel].data();
                        }
                    }

                    // We also reuse the output buffers to avoid some
                    // unnecessary heap allocations
                    if (!std::holds_alternative<std::vector<std::vector<T>>>(
                            process_response.buffers)) {
                        process_response.buffers
                            .emplace<std::vector<std::vector<T>>>();
                    }

                    std::vector<std::vector<T>>& output_audio_buffers =
                        std::get<std::vector<std::vector<T>>>(
                            process_response.buffers);
                    output_audio_buffers.resize(plugin->numOutputs);
                    for (size_t channel = 0;
                         channel < output_audio_buffers.size(); channel++) {
                        output_audio_buffers[channel].resize(
                            process_request.sample_frames);
                    }

                    // And the process functions also expect a `T**` for their
                    // outputs
                    thread_local std::vector<T*> output_pointers{};
                    if (output_pointers.size() != output_audio_buffers.size()) {
                        output_pointers.resize(output_audio_buffers.size());
                        for (size_t channel = 0;
                             channel < output_audio_buffers.size(); channel++) {
                            output_pointers[channel] =
                                output_audio_buffers[channel].data();
                        }
                    }

                    if constexpr (std::is_same_v<T, float>) {
                        // Any plugin made in the last fifteen years or so
                        // should support `processReplacing`. In the off chance
                        // it does not we can just emulate this behavior
                        // ourselves.
                        if (plugin->processReplacing) {
                            plugin->processReplacing(
                                plugin, input_pointers.data(),
                                output_pointers.data(),
                                process_request.sample_frames);
                        } else {
                            // If we zero out this buffer then the behavior is
                            // the same as `processReplacing`
                            for (std::vector<T>& buffer :
                                 output_audio_buffers) {
                                std::fill(buffer.begin(), buffer.end(), (T)0.0);
                            }

                            plugin->process(plugin, input_pointers.data(),
                                            output_pointers.data(),
                                            process_request.sample_frames);
                        }
                    } else if (std::is_same_v<T, double>) {
                        plugin->processDoubleReplacing(
                            plugin, input_pointers.data(),
                            output_pointers.data(),
                            process_request.sample_frames);
                    } else {
                        static_assert(
                            std::is_same_v<T, float> ||
                                std::is_same_v<T, double>,
                            "Audio processing only works with single and "
                            "double precision floating point numbers");
                    }
                },
                process_request.buffers);

            // We modified the buffers within the `process_response` object, so
            // we can just send that object back. Like on the plugin side we
            // cannot reuse the request object because a plugin may have a
            // different number of input and output channels
            sockets.host_vst_process_replacing.send(process_response, buffer);

            // See the docstrong on `should_clear_midi_events` for why we
            // don't just clear `next_buffer_midi_events` here
            should_clear_midi_events = true;
        });
    });
}

bool Vst2Bridge::inhibits_event_loop() noexcept {
    return !is_initialized;
}

void Vst2Bridge::run() {
    sockets.host_vst_dispatch.receive_events(
        std::nullopt, [&](Vst2Event& event, bool /*on_main_thread*/) {
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

                // See the docstring on `should_clear_midi_events` for why we
                // only deallocate old MIDI events here instead of a at the end
                // of every processing cycle
                if (should_clear_midi_events) {
                    next_audio_buffer_midi_events.clear();
                    should_clear_midi_events = false;
                }

                next_audio_buffer_midi_events.push_back(
                    std::get<DynamicVstEvents>(event.payload));
                DynamicVstEvents& events = next_audio_buffer_midi_events.back();

                // Exact same handling as in `passthrough_event()`, apart
                // from making a copy of the events first
                const intptr_t return_value = plugin->dispatcher(
                    plugin, event.opcode, event.index, event.value,
                    &events.as_c_events(), event.option);

                Vst2EventResult response{.return_value = return_value,
                                         .payload = nullptr,
                                         .value_payload = std::nullopt};

                return response;
            } else {
                return passthrough_event(
                    plugin,
                    [&](AEffect* plugin, int opcode, int index, intptr_t value,
                        void* data, float option) -> intptr_t {
                        // Certain functions will most definitely involve
                        // the GUI or the Win32 message loop. These
                        // functions have to be performed on the thread that
                        // is running the IO context, since this is also
                        // where the plugins were instantiated and where the
                        // Win32 message loop is handled.
                        if (unsafe_requests.contains(opcode)) {
                            return main_context
                                .run_in_context([&]() -> intptr_t {
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
                        } else if (safe_mutually_recursive_requests.contains(
                                       opcode)) {
                            // If this function call is potentially in response
                            // to a callback contained in
                            // `mutually_recursive_callbacks`, then we should
                            // call it on the same thread that called that
                            // callback if possible. This may be needed when
                            // plugins use recursive mutexes, thus causing
                            // deadlocks when the function is called from any
                            // other thread.
                            return mutual_recursion.handle([&]() {
                                return dispatch_wrapper(plugin, opcode, index,
                                                        value, data, option);
                            });
                        } else {
                            return dispatch_wrapper(plugin, opcode, index,
                                                    value, data, option);
                        }
                    },
                    event);
            }
        });
}

void Vst2Bridge::handle_x11_events() noexcept {
    if (editor) {
        editor->handle_x11_events();
    }
}

void Vst2Bridge::close_sockets() {
    sockets.close();
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

class HostCallbackDataConverter : public DefaultDataConverter {
   public:
    HostCallbackDataConverter(
        AEffect* plugin,
        VstTimeInfo& last_time_info,
        MutualRecursionHelper<Win32Thread>& mutual_recursion) noexcept
        : plugin(plugin),
          last_time_info(last_time_info),
          mutual_recursion(mutual_recursion) {}

    Vst2Event::Payload read_data(const int opcode,
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
            // NOTE: DefaultDataConverter::read() should be able to handle all
            //       of these 'simple' opcodes, but Plugsound Free by UVI passes
            //       random garbage for their data argument. Because of that
            //       `audioMasterWantMidi()` will segfault because when we'll
            //       try to read that data as a string we'll start reading
            //       unallocated memory. Even though no other plugins seem to do
            //       this< we'll list all of these data-less opcodes just to be
            //       sure. We're leaving out a few opcodes here, because I have
            //       no clue whether some of the more obscure ones are supposed
            //       to have an data argument or not.
            case audioMasterAutomate:
            case audioMasterVersion:
            case audioMasterCurrentId:
            case audioMasterIdle:
            case audioMasterWantMidi:
            case audioMasterSizeWindow:
            case audioMasterGetSampleRate:
            case audioMasterGetBlockSize:
            case audioMasterGetInputLatency:
            case audioMasterGetOutputLatency:
            case audioMasterGetCurrentProcessLevel:
            case audioMasterGetAutomationState:
            case audioMasterGetVendorVersion:
            case audioMasterGetLanguage:
            case audioMasterUpdateDisplay:
            case audioMasterBeginEdit:
            case audioMasterEndEdit:
                return nullptr;
                break;
            default:
                return DefaultDataConverter::read_data(opcode, index, value,
                                                       data);
                break;
        }
    }

    std::optional<Vst2Event::Payload> read_value(
        const int opcode,
        const intptr_t value) const override {
        return DefaultDataConverter::read_value(opcode, value);
    }

    void write_data(const int opcode,
                    void* data,
                    const Vst2EventResult& response) const override {
        switch (opcode) {
            case audioMasterGetTime:
                // If the host returned a valid `VstTimeInfo` object, then we'll
                // keep track of it so we can return a pointer to it in the
                // function below
                if (std::holds_alternative<VstTimeInfo>(response.payload)) {
                    last_time_info = std::get<VstTimeInfo>(response.payload);
                }
                break;
            default:
                DefaultDataConverter::write_data(opcode, data, response);
                break;
        }
    }

    intptr_t return_value(const int opcode,
                          const intptr_t original) const override {
        switch (opcode) {
            case audioMasterGetTime: {
                // If the host returned a null pointer, then we'll do the same
                // thing here
                if (original == 0) {
                    return 0;
                } else {
                    return reinterpret_cast<intptr_t>(&last_time_info);
                }
            } break;
            default:
                return DefaultDataConverter::return_value(opcode, original);
                break;
        }
    }

    void write_value(const int opcode,
                     intptr_t value,
                     const Vst2EventResult& response) const override {
        return DefaultDataConverter::write_value(opcode, value, response);
    }

    Vst2EventResult send_event(
        boost::asio::local::stream_protocol::socket& socket,
        const Vst2Event& event) const override {
        if (mutually_recursive_callbacks.contains(event.opcode)) {
            return mutual_recursion.fork([&]() {
                return DefaultDataConverter::send_event(socket, event);
            });
        } else {
            return DefaultDataConverter::send_event(socket, event);
        }
    }

   private:
    AEffect* plugin;
    VstTimeInfo& last_time_info;
    MutualRecursionHelper<Win32Thread>& mutual_recursion;
};

intptr_t Vst2Bridge::host_callback(AEffect* effect,
                                   int opcode,
                                   int index,
                                   intptr_t value,
                                   void* data,
                                   float option) {
    switch (opcode) {
        case audioMasterGetTime: {
            // During a processing call we'll have already sent the current
            // transport information from the plugin side to avoid an
            // unnecessary callback
            const VstTimeInfo* cached_time_info = time_info_cache.get();
            if (cached_time_info) {
                // This cached value is temporary, so we'll still use the
                // regular time info storing mechanism
                last_time_info = *cached_time_info;
                const intptr_t result =
                    reinterpret_cast<intptr_t>(&last_time_info);

                // Make sure that these cached events don't get lost in the logs
                logger.log_event(false, opcode, index, value,
                                 WantsVstTimeInfo{}, option, std::nullopt);
                logger.log_event_response(false, opcode, result, last_time_info,
                                          std::nullopt, true);

                return result;
            }
        } break;
        case audioMasterGetCurrentProcessLevel: {
            // We also send the current process level for similar reasons
            const int* current_process_level = process_level_cache.get();
            if (current_process_level) {
                logger.log_event(false, opcode, index, value, nullptr, option,
                                 std::nullopt);
                logger.log_event_response(false, opcode, *current_process_level,
                                          nullptr, std::nullopt, true);

                return *current_process_level;
            }
        } break;
    }

    HostCallbackDataConverter converter(effect, last_time_info,
                                        mutual_recursion);
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
