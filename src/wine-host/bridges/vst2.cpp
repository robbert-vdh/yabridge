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

#include "vst2.h"

#include <boost/asio/dispatch.hpp>
#include <future>
#include <iostream>

#include "../../common/communication.h"
#include "../../common/events.h"

/**
 * A function pointer to what should be the entry point of a VST plugin.
 */
using VstEntryPoint = AEffect*(VST_CALL_CONV*)(audioMasterCallback);

/**
 * This ugly global is needed so we can get the instance of a `Brdige` class
 * from an `AEffect` when it performs a host callback during its initialization.
 */
Vst2Bridge* current_bridge_instance = nullptr;
/**
 * Needed for the rare event that two plugins are getting initialized at the
 * same time.
 */
std::mutex current_bridge_instance_mutex;

intptr_t VST_CALL_CONV
host_callback_proxy(AEffect*, int, int, intptr_t, void*, float);

// We need to use the `CreateThread` WinAPI functions instead of `std::thread`
// to use the correct calling conventions within threads. Otherwise we'll get
// some rare and impossible to debug data races in some particular plugins.
uint32_t WINAPI handle_dispatch_midi_events_proxy(void*);
uint32_t WINAPI handle_parameters_proxy(void*);
uint32_t WINAPI handle_process_replacing_proxy(void*);

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

Vst2Bridge::Vst2Bridge(boost::asio::io_context& main_context,
                       std::string plugin_dll_path,
                       std::string socket_endpoint_path)
    : io_context(main_context),
      plugin_handle(LoadLibrary(plugin_dll_path.c_str()), FreeLibrary),
      socket_endpoint(socket_endpoint_path),
      host_vst_dispatch(io_context),
      host_vst_dispatch_midi_events(io_context),
      vst_host_callback(io_context),
      host_vst_parameters(io_context),
      host_vst_process_replacing(io_context),
      host_vst_control(io_context) {
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

    // It's very important that these sockets are accepted to in the same order
    // in the Linux plugin
    host_vst_dispatch.connect(socket_endpoint);
    host_vst_dispatch_midi_events.connect(socket_endpoint);
    vst_host_callback.connect(socket_endpoint);
    host_vst_parameters.connect(socket_endpoint);
    host_vst_process_replacing.connect(socket_endpoint);
    host_vst_control.connect(socket_endpoint);

    // Initialize after communication has been set up
    // We'll try to do the same `get_bridge_isntance` trick as in
    // `plugin/plugin.cpp`, but since the plugin will probably call the host
    // callback while it's initializing we sadly have to use a global here.
    {
        std::lock_guard lock(current_bridge_instance_mutex);
        current_bridge_instance = this;
        plugin = vst_entry_point(host_callback_proxy);
        if (!plugin) {
            throw std::runtime_error("VST plugin at '" + plugin_dll_path +
                                     "' failed to initialize.");
        }

        // We only needed this little hack during initialization
        current_bridge_instance = nullptr;
        plugin->ptr1 = this;
    }

    // Send the plugin's information to the Linux VST plugin. Any other updates
    // of this object will be sent over the `dispatcher()` socket. This would be
    // done after the host calls `effOpen()`, and when the plugin calls
    // `audioMasterIOChanged()`.
    write_object(host_vst_control, EventResult{.return_value = 0,
                                               .payload = *plugin,
                                               .value_payload = std::nullopt});

    // After sending the AEffect struct we'll receive this instance's
    // configuration as a response
    config = read_object<Configuration>(host_vst_control);

    // This works functionally identically to the `handle_dispatch()` function,
    // but this socket will only handle MIDI events and it will handle them
    // eagerly. This is needed because of Win32 API limitations.
    dispatch_midi_events_handler =
        Win32Thread(handle_dispatch_midi_events_proxy, this);

    parameters_handler = Win32Thread(handle_parameters_proxy, this);

    process_replacing_handler =
        Win32Thread(handle_process_replacing_proxy, this);
}

bool Vst2Bridge::should_skip_message_loop() const {
    return std::holds_alternative<EditorOpening>(editor);
}

void Vst2Bridge::handle_dispatch() {
    while (true) {
        try {
            receive_event(
                host_vst_dispatch, std::nullopt,
                passthrough_event(
                    plugin,
                    [&](AEffect* plugin, int opcode, int index, intptr_t value,
                        void* data, float option) -> intptr_t {
                        // Instead of running `plugin->dispatcher()` (or
                        // `dispatch_wrapper()`) directly, we'll run the
                        // function within the IO context so all events will be
                        // executed on the same thread as the one that runs the
                        // Win32 message loop
                        std::promise<intptr_t> dispatch_result;
                        boost::asio::dispatch(io_context, [&]() {
                            const intptr_t result = dispatch_wrapper(
                                plugin, opcode, index, value, data, option);

                            dispatch_result.set_value(result);
                        });

                        // The message loop and X11 event handling will be run
                        // separately on a timer
                        return dispatch_result.get_future().get();
                    }));
        } catch (const boost::system::system_error&) {
            // The plugin has cut off communications, so we can shut down this
            // host application
            break;
        }
    }
}

void Vst2Bridge::handle_dispatch_midi_events() {
    while (true) {
        try {
            receive_event(
                host_vst_dispatch_midi_events, std::nullopt, [&](Event& event) {
                    if (BOOST_LIKELY(event.opcode == effProcessEvents)) {
                        // For 99% of the plugins we can just call
                        // `effProcessReplacing()` and be done with it, but a
                        // select few plugins (I could only find Kontakt that
                        // does this) don't actually make copies of the events
                        // they receive and only store pointers, meaning that
                        // they have to live at least until the next audio
                        // buffer gets processed. We're not using
                        // `passhtourhg_events()` here directly because we need
                        // to store a copy of the `DynamicVstEvents` struct
                        // before passing the generated `VstEvents` object to
                        // the plugin.
                        std::lock_guard lock(next_buffer_midi_events_mutex);

                        next_audio_buffer_midi_events.push_back(
                            std::get<DynamicVstEvents>(event.payload));
                        DynamicVstEvents& events =
                            next_audio_buffer_midi_events.back();

                        // Exact same handling as in `passthrough_event`, apart
                        // from making a copy of the events first
                        const intptr_t return_value = plugin->dispatcher(
                            plugin, event.opcode, event.index, event.value,
                            &events.as_c_events(), event.option);

                        EventResult response{.return_value = return_value,
                                             .payload = nullptr,
                                             .value_payload = std::nullopt};

                        return response;
                    } else {
                        using namespace std::placeholders;

                        std::cerr << "[Warning] Received non-MIDI "
                                     "event on MIDI processing thread"
                                  << std::endl;

                        // Maybe this should just be a hard error instead, since
                        // it should never happen
                        return passthrough_event(
                            plugin,
                            std::bind(&Vst2Bridge::dispatch_wrapper, this, _1,
                                      _2, _3, _4, _5, _6))(event);
                    }
                });
        } catch (const boost::system::system_error&) {
            // The plugin has cut off communications, so we can shut down this
            // host application
            break;
        }
    }
}

void Vst2Bridge::handle_parameters() {
    while (true) {
        try {
            // Both `getParameter` and `setParameter` functions are passed
            // through on this socket since they have a lot of overlap. The
            // presence of the `value` field tells us which one we're dealing
            // with.
            auto request = read_object<Parameter>(host_vst_parameters);
            if (request.value) {
                // `setParameter`
                plugin->setParameter(plugin, request.index, *request.value);

                ParameterResult response{std::nullopt};
                write_object(host_vst_parameters, response);
            } else {
                // `getParameter`
                float value = plugin->getParameter(plugin, request.index);

                ParameterResult response{value};
                write_object(host_vst_parameters, response);
            }
        } catch (const boost::system::system_error&) {
            // The plugin has cut off communications, so we can shut down this
            // host application
            break;
        }
    }
}

void Vst2Bridge::handle_process_replacing() {
    // These are used as scratch buffers to prevent unnecessary allocations.
    // Since don't know in advance whether the host will call `processReplacing`
    // or `processDoubleReplacing` we'll just create both.
    std::vector<std::vector<float>> output_buffers_single_precision(
        plugin->numOutputs);
    std::vector<std::vector<double>> output_buffers_double_precision(
        plugin->numOutputs);

    while (true) {
        try {
            auto request = read_object<AudioBuffers>(host_vst_process_replacing,
                                                     process_buffer);
            // Let the plugin process the MIDI events that were received since
            // the last buffer, and then clean up those events. This approach
            // should not be needed but Kontakt only stores pointers to rather
            // than copies of the events.
            std::lock_guard lock(next_buffer_midi_events_mutex);

            // Since the host should only be calling one of `process()`,
            // processReplacing()` or `processDoubleReplacing()`, we can all
            // handle them over the same socket. We pick which one to call
            // depending on the type of data we got sent and the plugin's
            // reported support for these functions.
            std::visit(
                overload{
                    [&](std::vector<std::vector<float>>& input_buffers) {
                        // The process functions expect a `float**` for their
                        // inputs and their outputs
                        std::vector<float*> inputs;
                        for (auto& buffer : input_buffers) {
                            inputs.push_back(buffer.data());
                        }

                        // We reuse the buffers to avoid some unnecessary heap
                        // allocations, so we need to make sure the buffers are
                        // large enough since plugins can change their output
                        // configuration. The type we're using here (single
                        // precision floats vs double precisioon doubles) should
                        // be the same as the one we're sending in our response.
                        std::vector<float*> outputs;
                        output_buffers_single_precision.resize(
                            plugin->numOutputs);
                        for (auto& buffer : output_buffers_single_precision) {
                            buffer.resize(request.sample_frames);
                            outputs.push_back(buffer.data());
                        }

                        // Any plugin made in the last fifteen years or so
                        // should support `processReplacing`. In the off chance
                        // it does not we can just emulate this behavior
                        // ourselves.
                        if (plugin->processReplacing) {
                            plugin->processReplacing(plugin, inputs.data(),
                                                     outputs.data(),
                                                     request.sample_frames);
                        } else {
                            // If we zero out this buffer then the behavior is
                            // the same as `processReplacing``
                            for (std::vector<float>& buffer :
                                 output_buffers_single_precision) {
                                std::fill(buffer.begin(), buffer.end(), 0.0);
                            }

                            plugin->process(plugin, inputs.data(),
                                            outputs.data(),
                                            request.sample_frames);
                        }

                        AudioBuffers response{output_buffers_single_precision,
                                              request.sample_frames};
                        write_object(host_vst_process_replacing, response,
                                     process_buffer);
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
                        for (auto& buffer : output_buffers_double_precision) {
                            buffer.resize(request.sample_frames);
                            outputs.push_back(buffer.data());
                        }

                        plugin->processDoubleReplacing(plugin, inputs.data(),
                                                       outputs.data(),
                                                       request.sample_frames);

                        AudioBuffers response{output_buffers_double_precision,
                                              request.sample_frames};
                        write_object(host_vst_process_replacing, response,
                                     process_buffer);
                    }},
                request.buffers);

            next_audio_buffer_midi_events.clear();
        } catch (const boost::system::system_error&) {
            // The plugin has cut off communications, so we can shut down this
            // host application
            break;
        }
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
        case effEditGetRect: {
            // Some plugins will have a race condition if the message loops gets
            // handled between the call to `effEditGetRect()` and
            // `effEditOpen()`, although this behavior never appears on Windows
            // as hosts will always either call these functions in sequence or
            // in reverse. We need to temporarily stop handling messages when
            // this happens.
            if (!std::holds_alternative<Editor>(editor)) {
                editor = EditorOpening{};
            }

            return plugin->dispatcher(plugin, opcode, index, value, data,
                                      option);
        } break;
        case effEditOpen: {
            // Create a Win32 window through Wine, embed it into the window
            // provided by the host, and let the plugin embed itself into
            // the Wine window
            const auto x11_handle = reinterpret_cast<size_t>(data);
            // Win32 window classes have to be unique for the whole application.
            // When hosting multiple plugins in a group process, all plugins
            // should get a unique window class
            const std::string window_class =
                "yabridge plugin " + socket_endpoint.path();
            Editor& editor_instance = editor.emplace<Editor>(
                config, window_class, x11_handle, plugin);

            return plugin->dispatcher(plugin, opcode, index, value,
                                      editor_instance.get_win32_handle(),
                                      option);
        } break;
        case effEditClose: {
            const intptr_t return_value =
                plugin->dispatcher(plugin, opcode, index, value, data, option);

            // Cleanup is handled through RAII
            editor = std::monostate();

            return return_value;
        } break;
        default:
            return plugin->dispatcher(plugin, opcode, index, value, data,
                                      option);
            break;
    }
}

void Vst2Bridge::handle_win32_events() {
    std::visit(overload{[](Editor& editor) { editor.handle_win32_events(); },
                        [](std::monostate&) {
                            MSG msg;

                            for (int i = 0;
                                 i < max_win32_messages &&
                                 PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE);
                                 i++) {
                                TranslateMessage(&msg);
                                DispatchMessage(&msg);
                            }
                        },
                        [](EditorOpening&) {
                            // Don't handle any events in this particular case
                            // as explained in `Vst2Bridge::editor`
                        }},
               editor);
}

void Vst2Bridge::handle_x11_events() {
    std::visit(overload{[](Editor& editor) { editor.handle_x11_events(); },
                        [](auto&) {}},
               editor);
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
                // results back is done inside of `passthrough_event`.
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
                // make the function return a poitner to it in the function
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
    // HACK: Sadly this is needed to work around a mutual recursion issue with
    //       REAPER and Renoise. See #29 and #32.
    // TODO: We don't have access to the verbosity level here, but it would be
    //       nice to log that this is being skipped when `YABRIDGE_DEBUG_LEVEL
    //       >= 2`.
    if (config.hack_reaper_update_display &&
        opcode == audioMasterUpdateDisplay) {
        return 0;
    }

    HostCallbackDataConverter converter(effect, time_info);
    return send_event(vst_host_callback, host_callback_mutex, converter,
                      std::nullopt, opcode, index, value, data, option);
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

uint32_t WINAPI handle_dispatch_midi_events_proxy(void* instance) {
    static_cast<Vst2Bridge*>(instance)->handle_dispatch_midi_events();
    return 0;
}

uint32_t WINAPI handle_parameters_proxy(void* instance) {
    static_cast<Vst2Bridge*>(instance)->handle_parameters();
    return 0;
}

uint32_t WINAPI handle_process_replacing_proxy(void* instance) {
    static_cast<Vst2Bridge*>(instance)->handle_process_replacing();
    return 0;
}
