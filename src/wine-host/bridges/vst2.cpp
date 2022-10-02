// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2022 Robbert van der Helm
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

// Generated inside of the build directory
#include <version.h>

#include "../../common/communication/vst2.h"

/**
 * A function pointer to what should be the entry point of a VST plugin.
 */
using VstEntryPoint = AEffect*(VST_CALL_CONV*)(audioMasterCallback);

/**
 * If `plugin->ptr2` is set to this value, then we'll know that `plugin->ptr1`
 * is a valid pointer to a `Vst2Bridge` instance. This is needed for when one
 * instance of a plugin in a plugin group processes audio while another instance
 * of that plugin in the same plugin group is being initialized. In that
 * situation we cannot rely on just `current_bridge_instance`, and some plugins
 * don't zero initialize these pointers like they should so we also can't rely
 * on that.
 */
constexpr size_t yabridge_ptr2_magic = 0xdeadbeef + 420;

/**
 * This ugly global is needed so we can get the instance of a `Vst2Bridge` class
 * from an `AEffect` when it performs a host callback during its initialization.
 *
 * We don't need any locking here because we can only initialize `Vst2Bridge`
 * from the main thread anyways.
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
 *       will (wrongly) call `audioMasterUpdateDisplay()` during that
 *       call. Renoise then calls `effGetProgram()` during that which
 *       shouldn't cause any issues, but the Voxengo plugins try to lock
 *       recursive mutexes on both functions so `effGetProgram()` _has_ to be
 *       called on the same thread that is currently calling
 *       `audioMasterUpdateDisplay()`.
 * NOTE: Similarly, REAPER calls `effProgramName()` in response to
 *       `audioMasterUpdateDisplay()`, and PG-8X also requires that to be called
 *       from the same thread that called `audioMasterUpdateDisplay()`.
 */
static const std::unordered_set<int> mutually_recursive_callbacks{
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
static const std::unordered_set<int> safe_mutually_recursive_requests{
    effGetProgram, effGetProgramName, effGetProgramNameIndexed};

/**
 * Opcodes that should always be handled on the main thread because they may
 * involve GUI operations.
 *
 * NOTE: `effMainsChanged` is the odd one here. EZdrummer interacts with the
 *       Win32 message loop during this function. If we don't execute
 *       this from the main GUI thread, then EZdrummer won't produce any sound.
 * NOTE: `effSetChunk` and `effGetChunk` should be callable from any thread, but
 *       Algonaut Atlas doesn't restore chunk data unless `effSetChunk` is run
 *       from the GUI thread
 * NOTE: `effSetSampleRate` and `effSetBlockSize` really shouldn't be here, but
 *       New Sonic Arts' Vice plugin spawns a new thread and calls drawing code
 *       while changing sample rate and block size. We'll need to see if doing
 *       this on the main thread introduces any regressions.
 */
static const std::unordered_set<int> unsafe_requests{
    effOpen,          effClose,       effEditGetRect,   effEditOpen,
    effEditClose,     effEditIdle,    effEditTop,       effMainsChanged,
    effGetChunk,      effSetChunk,    effBeginLoadBank, effBeginLoadProgram,
    effSetSampleRate, effSetBlockSize};

/**
 * These opcodes from `unsafe_requests` should be run under realtime scheduling
 * so that if they spawn audio worker threads, those threads will also be run
 * with `SCHED_FIFO`. This is needed because unpatched Wine still does not
 * implement thread priorities. Normally these unsafe requests are run on the
 * main thread, which doesn't use realtime scheduling.
 */
static const std::unordered_set<int> unsafe_requests_realtime{effOpen,
                                                              effMainsChanged};

intptr_t VST_CALL_CONV
host_callback_proxy(AEffect*, int, int, intptr_t, void*, float);

/**
 * Fetch the Vst2Bridge instance stored in one of the two pointers reserved
 * for the host of the hosted VST plugin. This is sadly needed as a workaround
 * to avoid using globals since we need free function pointers to interface with
 * the VST C API.
 */
Vst2Bridge& get_bridge_instance(const AEffect* plugin) {
    if (plugin && reinterpret_cast<size_t>(plugin->ptr2) == yabridge_ptr2_magic)
        [[likely]] {
        return *static_cast<Vst2Bridge*>(plugin->ptr1);
    }

    // We can only set this pointer after the plugin has initialized, so when
    // the plugin performs a callback during its initialization we'll use the
    // current bridge instance set during the Vst2Bridge constructor. This is
    // thread safe because VST2 plugins have to be initialized on the main
    // thread.
    assert(current_bridge_instance);
    return *current_bridge_instance;
}

// FIXME: GCC 11/12 throws a false positive here for the
//        `time_info_cache_guard`:
//        https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80635
//
//        Oh and Clang doesn't know about -Wmaybe-uninitialized, so we need to
//        ignore some more warnings here to get clangd to not complain
#pragma GCC diagnostic push
#if defined(__GNUC__) && !defined(__llvm__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

Vst2Bridge::Vst2Bridge(MainContext& main_context,
                       // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
                       std::string plugin_dll_path,
                       std::string endpoint_base_dir,
                       pid_t parent_pid)
    : HostBridge(main_context, plugin_dll_path, parent_pid),
      logger_(generic_logger_),
      plugin_handle_(LoadLibrary(plugin_dll_path.c_str()), FreeLibrary),
      sockets_(main_context.context_, endpoint_base_dir, false) {
    if (!plugin_handle_) {
        throw std::runtime_error("Could not load the Windows .dll file at '" +
                                 plugin_dll_path + "'");
    }

    // VST plugin entry point functions should be called `VSTPluginMain`, but
    // pre-VST2.4 `main` was also a valid name
    VstEntryPoint vst_entry_point = nullptr;
    for (auto name : {"VSTPluginMain", "main"}) {
        vst_entry_point =
            reinterpret_cast<VstEntryPoint>(reinterpret_cast<size_t>(
                GetProcAddress(plugin_handle_.get(), name)));

        if (vst_entry_point) {
            break;
        }
    }
    if (!vst_entry_point) {
        throw std::runtime_error(
            "Could not find a valid VST entry point for '" + plugin_dll_path +
            "'.");
    }

    sockets_.connect();

    // We'll try to do the same `get_bridge_instance()` trick as in
    //`plugin/bridges/vst2.cpp`, but since the plugin will probably call the
    // host callback while it's initializing we sadly have to use a global here.
    // Note that this reinterpret cast is not needed at all since the function
    // pointer types are exactly the same, but clangd will complain otherwise
    current_bridge_instance = this;

    // We'll also need to make sure that any audio worker threads created by the
    // plugin are running using realtime scheduling, since Wine doesn't fully
    // implement the Win32 process priority API yet.
    set_realtime_priority(true);
    plugin_ = vst_entry_point(
        reinterpret_cast<audioMasterCallback>(host_callback_proxy));
    set_realtime_priority(false);

    if (!plugin_) {
        throw std::runtime_error("VST plugin at '" + plugin_dll_path +
                                 "' failed to initialize.");
    }

    // We use `plugin->ptr2` to identify plugins that have already been
    // initialized. Otherwise we can run into thread safety issues when a plugin
    // is processing audio while another plugin is being initialized.
    current_bridge_instance = nullptr;
    plugin_->ptr1 = this;
    plugin_->ptr2 = reinterpret_cast<void*>(yabridge_ptr2_magic);

    // Send the plugin's information to the Linux VST plugin. Any other updates
    // of this object will be sent over the `dispatcher()` socket. This would be
    // done after the host calls `effOpen()`, and when the plugin calls
    // `audioMasterIOChanged()`. We will also send along this host's version so
    // we can show a warning when the plugin's version doesn't match.
    sockets_.host_plugin_control_.send(
        Vst2EventResult{.return_value = 0,
                        .payload = *plugin_,
                        .value_payload = yabridge_git_version});

    // After sending the AEffect struct we'll receive this instance's
    // configuration as a response
    config_ = sockets_.host_plugin_control_.receive_single<Configuration>();

    // Allow this plugin to configure the main context's tick rate
    main_context.update_timer_interval(config_.event_loop_interval());

    parameters_handler_ = Win32Thread([&]() {
        set_realtime_priority(true);
        pthread_setname_np(pthread_self(), "parameters");

        sockets_.host_plugin_parameters_.receive_multi<Parameter>(
            [&](Parameter& request, SerializationBufferBase& buffer) {
                // Both `getParameter` and `setParameter` functions are passed
                // through on this socket since they have a lot of overlap. The
                // presence of the `value` field tells us which one we're
                // dealing with.
                if (request.value) {
                    // `setParameter`
                    plugin_->setParameter(plugin_, request.index,
                                          *request.value);

                    ParameterResult response{std::nullopt};
                    sockets_.host_plugin_parameters_.send(response, buffer);
                } else {
                    // `getParameter`
                    float value = plugin_->getParameter(plugin_, request.index);

                    ParameterResult response{value};
                    sockets_.host_plugin_parameters_.send(response, buffer);
                }
            });
    });

    process_replacing_handler_ = Win32Thread([&]() {
        set_realtime_priority(true);
        pthread_setname_np(pthread_self(), "audio");

        // Most plugins will already enable FTZ, but there are a handful of
        // plugins that don't that suffer from extreme DSP load increases when
        // they start producing denormals
        ScopedFlushToZero ftz_guard;

        sockets_.host_plugin_process_replacing_.receive_multi<
            Vst2ProcessRequest>([&](Vst2ProcessRequest& process_request,
                                    SerializationBufferBase& buffer) {
            // Since the value cannot change during this processing cycle,
            // we'll send the current transport information as part of the
            // request so we prefetch it to avoid unnecessary callbacks from
            // the audio thread
            std::optional<decltype(time_info_cache_)::Guard>
                time_info_cache_guard =
                    process_request.current_time_info
                        ? std::optional(time_info_cache_.set(
                              *process_request.current_time_info))
                        : std::nullopt;

            // We'll also prefetch the process level, since some plugins
            // will ask for this during every processing cycle
            decltype(process_level_cache_)::Guard process_level_cache_guard =
                process_level_cache_.set(process_request.current_process_level);

            // As suggested by Jack Winter, we'll synchronize this thread's
            // audio processing priority with that of the host's audio
            // thread every once in a while
            if (process_request.new_realtime_priority) {
                set_realtime_priority(true,
                                      *process_request.new_realtime_priority);
            }

            // Let the plugin process the MIDI events that were received
            // since the last buffer, and then clean up those events. This
            // approach should not be needed but Kontakt only stores
            // pointers to rather than copies of the events.
            std::lock_guard lock(next_buffer_midi_events_mutex_);

            // As an optimization we no don't pass the input audio along
            // with `Vst2ProcessRequest`, and instead we'll write it to a
            // shared memory object on the plugin side. We can then write
            // the output audio to the same shared memory object. Since the
            // host should only be calling one of `process()`,
            // processReplacing()` or `processDoubleReplacing()`, we can all
            // handle them all at once. We pick which one to call depending
            // on the type of data we got sent and the plugin's reported
            // support for these functions.
            auto do_process = [&]<typename T>(T) {
                // These were set up after the host called
                // `effMainsChanged()` with the correct size, so this
                // reinterpret cast is safe even if the host suddenly starts
                // sending 32-bit single precision audio after it set up
                // audio processing for double precision (not that the
                // Windows VST2 plugin would be able to handle that,
                // presumably)
                T** input_channel_pointers = reinterpret_cast<T**>(
                    process_buffers_input_pointers_.data());
                T** output_channel_pointers = reinterpret_cast<T**>(
                    process_buffers_output_pointers_.data());

                if constexpr (std::is_same_v<T, float>) {
                    // Any plugin made in the last fifteen years or so
                    // should support `processReplacing`. In the off chance
                    // it does not we can just emulate this behavior
                    // ourselves.
                    if (plugin_->processReplacing) {
                        plugin_->processReplacing(
                            plugin_, input_channel_pointers,
                            output_channel_pointers,
                            process_request.sample_frames);
                    } else {
                        // If we zero out this buffer then the behavior is
                        // the same as `processReplacing`
                        for (int channel = 0; channel < plugin_->numOutputs;
                             channel++) {
                            std::fill(output_channel_pointers[channel],
                                      output_channel_pointers[channel] +
                                          process_request.sample_frames,
                                      static_cast<T>(0.0));
                        }

                        plugin_->process(plugin_, input_channel_pointers,
                                         output_channel_pointers,
                                         process_request.sample_frames);
                    }
                } else if (std::is_same_v<T, double>) {
                    plugin_->processDoubleReplacing(
                        plugin_, input_channel_pointers,
                        output_channel_pointers, process_request.sample_frames);
                } else {
                    static_assert(
                        std::is_same_v<T, float> || std::is_same_v<T, double>,
                        "Audio processing only works with single and "
                        "double precision floating point numbers");
                }
            };

            assert(process_buffers_);
            if (process_request.double_precision) {
                // XXX: Clangd doesn't let you specify template parameters
                //      for templated lambdas. This argument should get
                //      optimized out
                do_process(double());
            } else {
                do_process(float());
            }

            // We modified the buffers within the `process_response` object,
            // so we can just send that object back. Like on the plugin side
            // we cannot reuse the request object because a plugin may have
            // a different number of input and output channels
            sockets_.host_plugin_process_replacing_.send(Ack{}, buffer);

            // See the docstrong on `should_clear_midi_events` for why we
            // don't just clear `next_buffer_midi_events` here
            should_clear_midi_events_ = true;
        });
    });
}

#pragma GCC diagnostic pop

bool Vst2Bridge::inhibits_event_loop() noexcept {
    return !is_initialized_;
}

void Vst2Bridge::run() {
    set_realtime_priority(true);

    sockets_.host_plugin_dispatch_.receive_events(
        std::nullopt,
        [&](Vst2Event& event, bool /*on_main_thread*/) -> Vst2EventResult {
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
                std::lock_guard lock(next_buffer_midi_events_mutex_);

                // See the docstring on `should_clear_midi_events` for why we
                // only deallocate old MIDI events here instead of a at the end
                // of every processing cycle
                if (should_clear_midi_events_) {
                    next_audio_buffer_midi_events_.clear();
                    should_clear_midi_events_ = false;
                }

                next_audio_buffer_midi_events_.push_back(
                    std::get<DynamicVstEvents>(event.payload));
                DynamicVstEvents& events =
                    next_audio_buffer_midi_events_.back();

                // Exact same handling as in `passthrough_event()`, apart from
                // making a copy of the events first
                const intptr_t return_value = plugin_->dispatcher(
                    plugin_, event.opcode, event.index, event.value,
                    &events.as_c_events(), event.option);

                return Vst2EventResult{.return_value = return_value,
                                       .payload = nullptr,
                                       .value_payload = std::nullopt};
            }

            Vst2EventResult result = passthrough_event(
                plugin_,
                [&](AEffect* plugin, int opcode, int index, intptr_t value,
                    void* data, float option) -> intptr_t {
                    // Certain functions will most definitely involve
                    // the GUI or the Win32 message loop. These
                    // functions have to be performed on the thread that
                    // is running the IO context, since this is also
                    // where the plugins were instantiated and where the
                    // Win32 message loop is handled.
                    if (unsafe_requests.contains(opcode)) {
                        // Requests that potentially spawn an audio worker
                        // thread should be run with `SCHED_FIFO` until Wine
                        // implements the corresponding Windows API
                        const bool is_realtime_request =
                            unsafe_requests_realtime.contains(opcode);

                        return main_context_
                            .run_in_context([&]() -> intptr_t {
                                if (is_realtime_request) {
                                    set_realtime_priority(true);
                                }

                                const intptr_t result = dispatch_wrapper(
                                    plugin, opcode, index, value, data, option);

                                if (is_realtime_request) {
                                    set_realtime_priority(false);
                                }

                                // The Win32 message loop will not be run up to
                                // this point to prevent plugins with partially
                                // initialized states from misbehaving
                                if (opcode == effOpen) {
                                    is_initialized_ = true;
                                }

                                return result;
                            })
                            .get();
                    } else if (safe_mutually_recursive_requests.contains(
                                   opcode)) {
                        // If this function call is potentially in response to a
                        // callback contained in `mutually_recursive_callbacks`,
                        // then we should call it on the same thread that called
                        // that callback if possible. This may be needed when
                        // plugins use recursive mutexes, thus causing deadlocks
                        // when the function is called from any other thread.
                        return mutual_recursion_.handle([&]() {
                            return dispatch_wrapper(plugin, opcode, index,
                                                    value, data, option);
                        });
                    } else {
                        return dispatch_wrapper(plugin, opcode, index, value,
                                                data, option);
                    }
                },
                event);

            // We also need some special handling to set up audio processing.
            // After the plugin has finished setting up audio processing, we'll
            // initialize our shared audio buffers on this side and send the
            // configuration back to the native plugin so it can also connect to
            // the same buffers. We cannot use `Vst2Bridge::dispatch_wrapper()`
            // for this because we need to directly return payload data that
            // won't be visible to the plugin at all.
            // NOTE: Ardour will call `effMainsChanged()` with a value of 1
            //       unconditionally when unloading a plugin, even when audio
            //       playback has never been initialized (and `effSetBlockSize`
            //       has never been called)
            if (event.opcode == effMainsChanged && event.value == 1) {
                // Returning another result this way is a bit ugly, but sadly
                // optimizations have never made code nicer to read
                return Vst2EventResult{.return_value = result.return_value,
                                       .payload = setup_shared_audio_buffers(),
                                       .value_payload = std::nullopt};
            }

            return result;
        });
}

void Vst2Bridge::close_sockets() {
    sockets_.close();
}

class HostCallbackDataConverter : public DefaultDataConverter {
   public:
    HostCallbackDataConverter(
        AEffect* plugin,
        VstTimeInfo& last_time_info,
        MutualRecursionHelper<Win32Thread>& mutual_recursion) noexcept
        : plugin_(plugin),
          last_time_info_(last_time_info),
          mutual_recursion_(mutual_recursion) {}

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
                return AEffect(*plugin_);
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
            // NOTE: REAPER abuses the dispatcher to add their own opcodes
            //       outside of `audioMasterVendorSpecific`
            case audioMasterDeadBeef:
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
                    last_time_info_ = std::get<VstTimeInfo>(response.payload);
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
                    return reinterpret_cast<intptr_t>(&last_time_info_);
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

    Vst2EventResult send_event(asio::local::stream_protocol::socket& socket,
                               const Vst2Event& event,
                               SerializationBufferBase& buffer) const override {
        if (mutually_recursive_callbacks.contains(event.opcode)) {
            return mutual_recursion_.fork([&]() {
                return DefaultDataConverter::send_event(socket, event, buffer);
            });
        } else {
            return DefaultDataConverter::send_event(socket, event, buffer);
        }
    }

   private:
    AEffect* plugin_;
    VstTimeInfo& last_time_info_;
    MutualRecursionHelper<Win32Thread>& mutual_recursion_;
};

intptr_t Vst2Bridge::host_callback(AEffect* effect,
                                   int opcode,
                                   int index,
                                   intptr_t value,
                                   void* data,
                                   float option) {
    switch (opcode) {
        // During a processing call we'll have already sent the current
        // transport information from the plugin side to avoid an unnecessary
        // callback
        case audioMasterGetTime: {
            const VstTimeInfo* cached_time_info = time_info_cache_.get();
            if (cached_time_info) {
                // This cached value is temporary, so we'll still use the
                // regular time info storing mechanism
                last_time_info_ = *cached_time_info;
                const intptr_t result =
                    reinterpret_cast<intptr_t>(&last_time_info_);

                // Make sure that these cached events don't get lost in the logs
                logger_.log_event(false, opcode, index, value,
                                  WantsVstTimeInfo{}, option, std::nullopt);
                logger_.log_event_response(false, opcode, result,
                                           last_time_info_, std::nullopt, true);

                return result;
            }
        } break;
        // We also send the current process level for similar reasons
        case audioMasterGetCurrentProcessLevel: {
            const int* current_process_level = process_level_cache_.get();
            if (current_process_level) {
                logger_.log_event(false, opcode, index, value, nullptr, option,
                                  std::nullopt);
                logger_.log_event_response(false, opcode,
                                           *current_process_level, nullptr,
                                           std::nullopt, true);

                return *current_process_level;
            }
        } break;
        // If the plugin changes its window size, we'll also resize the wrapper
        // window accordingly.
        case audioMasterSizeWindow: {
            if (editor_) {
                editor_->resize(index, value);
            }
        } break;
    }

    HostCallbackDataConverter converter(effect, last_time_info_,
                                        mutual_recursion_);
    return sockets_.plugin_host_callback_.send_event(
        converter, std::nullopt, opcode, index, value, data, option);
}

intptr_t Vst2Bridge::dispatch_wrapper(AEffect* plugin,
                                      int opcode,
                                      int index,
                                      intptr_t value,
                                      void* data,
                                      float option) {
    // We have to intercept GUI open calls since we can't use the X11 window
    // handle passed by the host. Keep in mind that in our `run()` function
    // above some of these events will be called on some arbitrary thread (where
    // we're running with realtime scheduling) and some might be called on the
    // main thread using `main_context.run_in_context()` (where we don't use
    // realtime scheduling).
    switch (opcode) {
        case effSetBlockSize: {
            // Used to initialize the shared audio buffers when handling
            // `effMainsChanged` in `Vst2Bridge::run()`
            max_samples_per_block_ = value;

            return plugin->dispatcher(plugin, opcode, index, value, data,
                                      option);
        } break;
        case effEditOpen: {
            // Create a Win32 window through Wine, embed it into the window
            // provided by the host, and let the plugin embed itself into
            // the Wine window
            const auto x11_handle = reinterpret_cast<size_t>(data);

            Editor& editor_instance = editor_.emplace(
                main_context_, config_, generic_logger_, x11_handle,
                [plugin = plugin_]() {
                    plugin->dispatcher(plugin, effEditIdle, 0, 0, nullptr, 0.0);
                });

            const intptr_t result =
                plugin->dispatcher(plugin, opcode, index, value,
                                   editor_instance.get_win32_handle(), option);

            // Make sure the wrapper window has the correct initial size. The
            // plugin can later change this size using `audioMasterSizeWindow`.
            // NOTE: Every single plugin handles `effEditGetRect` before
            //       `effEditOpen` fine. Except for this one single plugin:
            //       https://codefn42.com/randarp/index.html
            VstRect* editor_rect = nullptr;
            plugin->dispatcher(plugin, effEditGetRect, 0, 0, &editor_rect, 0.0);
            if (editor_rect) {
                editor_->resize(editor_rect->right - editor_rect->left,
                                editor_rect->bottom - editor_rect->top);
            }

            // NOTE: There's zero reason why the window couldn't already be
            //       visible from the start, but Waves V13 VST3 plugins think it
            //       would be a splendid idea to randomly dereference null
            //       pointers when the window is already visible. Thanks Waves.
            editor_instance.show();

            return result;
        } break;
        case effEditClose: {
            // Cleanup is handled through RAII
            const intptr_t return_value =
                plugin->dispatcher(plugin, opcode, index, value, data, option);
            editor_.reset();

            return return_value;
        } break;
        case effSetProcessPrecision: {
            // Used to initialize the shared audio buffers when handling
            // `effMainsChanged` in `Vst2Bridge::run()`
            double_precision_ = value == kVstProcessPrecision64;

            return plugin->dispatcher(plugin, opcode, index, value, data,
                                      option);
            break;
        }
        default: {
            return plugin->dispatcher(plugin, opcode, index, value, data,
                                      option);
            break;
        }
    }
}

AudioShmBuffer::Config Vst2Bridge::setup_shared_audio_buffers() {
    assert(max_samples_per_block_);

    // We'll first compute the size and channel offsets for our buffer based on
    // the information already passed to us by the host. The offsets for each
    // audio channel are in bytes because CLAP allows some ports to be 32-bit
    // only while other others are mixed 32-bit and 64-bit if the plugin opts in
    // to it, and the plugin only knows what format it receives during the
    // process call.
    const size_t sample_size =
        (double_precision_ ? sizeof(double) : sizeof(float));

    uint32_t current_offset = 0;

    std::vector<uint32_t> input_channel_offsets(plugin_->numInputs);
    for (int channel = 0; channel < plugin_->numInputs; channel++) {
        input_channel_offsets[channel] = current_offset;
        current_offset += *max_samples_per_block_ * sample_size;
    }

    std::vector<uint32_t> output_channel_offsets(plugin_->numOutputs);
    for (int channel = 0; channel < plugin_->numOutputs; channel++) {
        output_channel_offsets[channel] = current_offset;
        current_offset += *max_samples_per_block_ * sample_size;
    }

    // The size of the buffer is in bytes, and it will depend on whether the
    // host is going to pass 32-bit or 64-bit audio to the plugin
    const uint32_t buffer_size = current_offset;

    // We'll set up these shared memory buffers on the Wine side first, and then
    // when this request returns we'll do the same thing on the native plugin
    // side
    AudioShmBuffer::Config buffer_config{
        .name = sockets_.base_dir_.filename().string(),
        .size = buffer_size,
        .input_offsets = {std::move(input_channel_offsets)},
        .output_offsets = {std::move(output_channel_offsets)}};
    if (!process_buffers_) {
        process_buffers_.emplace(buffer_config);
    } else {
        process_buffers_->resize(buffer_config);
    }

    // The process functions expect a `T**` for their inputs and outputs, so
    // we'll also set those up right now
    process_buffers_input_pointers_.resize(plugin_->numInputs);
    for (int channel = 0; channel < plugin_->numInputs; channel++) {
        if (double_precision_) {
            process_buffers_input_pointers_[channel] =
                process_buffers_->input_channel_ptr<double>(0, channel);
        } else {
            process_buffers_input_pointers_[channel] =
                process_buffers_->input_channel_ptr<float>(0, channel);
        }
    }

    process_buffers_output_pointers_.resize(plugin_->numOutputs);
    for (int channel = 0; channel < plugin_->numOutputs; channel++) {
        if (double_precision_) {
            process_buffers_output_pointers_[channel] =
                process_buffers_->output_channel_ptr<double>(0, channel);
        } else {
            process_buffers_output_pointers_[channel] =
                process_buffers_->output_channel_ptr<float>(0, channel);
        }
    }

    return buffer_config;
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
