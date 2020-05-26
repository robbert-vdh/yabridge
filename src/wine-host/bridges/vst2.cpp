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
    if (current_bridge_instance != nullptr) {
        return *current_bridge_instance;
    }

    return *static_cast<Vst2Bridge*>(plugin->ptr1);
}

Vst2Bridge::Vst2Bridge(std::string plugin_dll_path,
                       std::string socket_endpoint_path)
    // See `plugin_handle`s docstring for information on why we're leaking
    // memory here
    // : plugin_handle(LoadLibrary(plugin_dll_path.c_str()), FreeLibrary),
    : plugin_handle(LoadLibrary(plugin_dll_path.c_str())),
      io_context(),
      socket_endpoint(socket_endpoint_path),
      host_vst_dispatch(io_context),
      host_vst_dispatch_midi_events(io_context),
      vst_host_callback(io_context),
      host_vst_parameters(io_context),
      host_vst_process_replacing(io_context) {
    // Got to love these C APIs
    if (plugin_handle == nullptr) {
        throw std::runtime_error("Could not load the Windows .dll file at '" +
                                 plugin_dll_path + "'");
    }

    // VST plugin entry point functions should be called `VSTPluginMain`, but
    // there are some older deprecated names that legacy plugins may still use
    VstEntryPoint vst_entry_point = nullptr;
    for (auto name : {"VSTPluginMain", "main_plugin", "main"}) {
        vst_entry_point = reinterpret_cast<VstEntryPoint>(
            reinterpret_cast<size_t>(GetProcAddress(plugin_handle, name)));

        if (vst_entry_point != nullptr) {
            break;
        }
    }
    if (vst_entry_point == nullptr) {
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

    // Initialize after communication has been set up
    // We'll try to do the same `get_bridge_isntance` trick as in
    // `plugin/plugin.cpp`, but since the plugin will probably call the host
    // callback while it's initializing we sadly have to use a global here.
    {
        std::lock_guard lock(current_bridge_instance_mutex);
        current_bridge_instance = this;
        plugin = vst_entry_point(host_callback_proxy);
        if (plugin == nullptr) {
            throw std::runtime_error("VST plugin at '" + plugin_dll_path +
                                     "' failed to initialize.");
        }

        // We only needed this little hack during initialization
        current_bridge_instance = nullptr;
        plugin->ptr1 = this;
    }

    // Send the plugin's information to the Linux VST plugin. This is done over
    // the `dispatch()` socket since this has to be done only once during
    // initialization. Any updates during runtime are handled using the
    // `audioMasterIOChanged` host callback.
    write_object(host_vst_dispatch, EventResult{0, *plugin, std::nullopt});

    // This works functionally identically to the `handle_dispatch_single()`
    // function below, but this socket will only handle MIDI events. This is
    // needed because of Win32 API limitations.
    dispatch_midi_events_handler =
        Win32Thread(handle_dispatch_midi_events_proxy, this);

    parameters_handler = Win32Thread(handle_parameters_proxy, this);

    process_replacing_handler =
        Win32Thread(handle_process_replacing_proxy, this);
}

bool Vst2Bridge::should_skip_message_loop() {
    return editor_is_opening;
}

void Vst2Bridge::handle_dispatch_single() {
    using namespace std::placeholders;

    // For our communication we use simple threads and blocking operations
    // instead of asynchronous IO since communication has to be handled in
    // lockstep anyway
    try {
        while (true) {
            receive_event(host_vst_dispatch, std::nullopt,
                          passthrough_event(
                              plugin, std::bind(&Vst2Bridge::dispatch_wrapper,
                                                this, _1, _2, _3, _4, _5, _6)));

            handle_win32_events();
            handle_x11_events();
        }
    } catch (const boost::system::system_error&) {
        // The plugin has cut off communications, so we can shut down this host
        // application
    }
}

void Vst2Bridge::handle_dispatch_multi(boost::asio::io_context& main_context) {
    // This works exactly the same as the function above, but execute the
    // actual event and run the message loop from the main thread that's
    // also instantiating these plugins. This is required for a few plugins
    // to run multiple instances in the same process
    try {
        while (true) {
            receive_event(
                host_vst_dispatch, std::nullopt,
                passthrough_event(
                    plugin,
                    [&](AEffect* plugin, int opcode, int index, intptr_t value,
                        void* data, float option) -> intptr_t {
                        std::promise<intptr_t> dispatch_result;
                        boost::asio::dispatch(main_context, [&]() {
                            const intptr_t result = dispatch_wrapper(
                                plugin, opcode, index, value, data, option);

                            dispatch_result.set_value(result);
                        });

                        // The message loop and X11 event handling will be run
                        // separately on a timer
                        return dispatch_result.get_future().get();
                    }));
        }
    } catch (const boost::system::system_error&) {
        // The plugin has cut off communications, so we can shut down this
        // host application
    }
}

void Vst2Bridge::handle_dispatch_midi_events() {
    try {
        while (true) {
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

                        EventResult response{return_value, nullptr,
                                             std::nullopt};

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
        }
    } catch (const boost::system::system_error&) {
        // The plugin has cut off communications, so we can shut down this host
        // application
    }
}

void Vst2Bridge::handle_parameters() {
    try {
        while (true) {
            // Both `getParameter` and `setParameter` functions are passed
            // through on this socket since they have a lot of overlap. The
            // presence of the `value` field tells us which one we're dealing
            // with.
            auto request = read_object<Parameter>(host_vst_parameters);
            if (request.value.has_value()) {
                // `setParameter`
                plugin->setParameter(plugin, request.index,
                                     request.value.value());

                ParameterResult response{std::nullopt};
                write_object(host_vst_parameters, response);
            } else {
                // `getParameter`
                float value = plugin->getParameter(plugin, request.index);

                ParameterResult response{value};
                write_object(host_vst_parameters, response);
            }
        }
    } catch (const boost::system::system_error&) {
        // The plugin has cut off communications, so we can shut down this host
        // application
    }
}

void Vst2Bridge::handle_process_replacing() {
    std::vector<std::vector<float>> output_buffers(plugin->numOutputs);

    try {
        while (true) {
            auto request = read_object<AudioBuffers>(host_vst_process_replacing,
                                                     process_buffer);

            // The process functions expect a `float**` for their inputs and
            // their outputs
            std::vector<float*> inputs;
            for (auto& buffer : request.buffers) {
                inputs.push_back(buffer.data());
            }

            // We reuse the buffers to avoid some unnecessary heap allocations,
            // so we need to make sure the buffers are large enough since
            // plugins can change their output configuration
            std::vector<float*> outputs;
            output_buffers.resize(plugin->numOutputs);
            for (auto& buffer : output_buffers) {
                buffer.resize(request.sample_frames);
                outputs.push_back(buffer.data());
            }

            // Let the plugin process the MIDI events that were received since
            // the last buffer, and then clean up those events. This approach
            // should not be needed but Kontakt only stores pointers to rather
            // than copies of the events.
            {
                std::lock_guard lock(next_buffer_midi_events_mutex);

                // Any plugin made in the last fifteen years or so should
                // support `processReplacing`. In the off chance it does not we
                // can just emulate this behavior ourselves.
                if (plugin->processReplacing != nullptr) {
                    plugin->processReplacing(plugin, inputs.data(),
                                             outputs.data(),
                                             request.sample_frames);
                } else {
                    // If we zero out this buffer then the behavior is the same
                    // as `processReplacing``
                    for (std::vector<float>& buffer : output_buffers) {
                        std::fill(buffer.begin(), buffer.end(), 0.0);
                    }

                    plugin->process(plugin, inputs.data(), outputs.data(),
                                    request.sample_frames);
                }

                next_audio_buffer_midi_events.clear();
            }

            AudioBuffers response{output_buffers, request.sample_frames};
            write_object(host_vst_process_replacing, response, process_buffer);
        }
    } catch (const boost::system::system_error&) {
        // The plugin has cut off communications, so we can shut down this host
        // application
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
            // `effEditOpen()`, since this won't ever happen on Windows and
            // plugins thus assume that this can't happen at all. If
            // `effEditOpen()` has not yet been called, then we'll mark the
            // editor as currently opening to prevent the message loop from
            // running.
            editor_is_opening = !editor.has_value();

            return plugin->dispatcher(plugin, opcode, index, value, data,
                                      option);
        } break;
        case effEditOpen: {
            // As explained above, if `effEditGetRect()` was called first then
            // after this the editor has finally been opened. Otherwise if this
            // function was first then we'll say that the editor is in the
            // process of being opened as the host will call `effEditGetRect()`
            // next.
            // TODO: Without plugin groups only the `effEditGetRect()` ->
            //       `effEditOpen()`, is skipping the message loop in between
            //       `effEditOpen()` and `effEditGetRect()` really needed?
            editor_is_opening = !editor_is_opening;

            // Create a Win32 window through Wine, embed it into the window
            // provided by the host, and let the plugin embed itself into
            // the Wine window
            const auto x11_handle = reinterpret_cast<size_t>(data);
            // Win32 window classes have to be unique for the whole application.
            // When hosting multiple plugins in a group process, all plugins
            // should get a unique window class
            const std::string window_class =
                "yabridge plugin " + socket_endpoint.path();

            editor.emplace(window_class, plugin, x11_handle);
            return plugin->dispatcher(plugin, opcode, index, value,
                                      editor->win32_handle.get(), option);
        } break;
        case effEditClose: {
            const intptr_t return_value =
                plugin->dispatcher(plugin, opcode, index, value, data, option);

            // Cleanup is handled through RAII
            editor.reset();

            return return_value;
        } break;
        default:
            return plugin->dispatcher(plugin, opcode, index, value, data,
                                      option);
            break;
    }
}

void Vst2Bridge::handle_win32_events() {
    // Don't run them message loop during the two step process of opening the
    // plugin editor since some plugins don't expect this
    if (should_skip_message_loop()) {
        return;
    }

    if (editor.has_value()) {
        editor->handle_win32_events();
    } else {
        MSG msg;

        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

void Vst2Bridge::handle_x11_events() {
    if (editor.has_value()) {
        editor->handle_x11_events();
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
                      const void* data) {
        switch (opcode) {
            case audioMasterGetTime:
                return WantsVstTimeInfo{};
                break;
            case audioMasterIOChanged:
                // This is a helpful event that indicates that the VST plugin's
                // `AEffect` struct has changed. Writing these results back is
                // done inside of `passthrough_event`.
                return AEffect(*plugin);
                break;
            case audioMasterProcessEvents:
                return DynamicVstEvents(*static_cast<const VstEvents*>(data));
                break;
            // We detect whether an opcode should return a string by checking
            // whether there's a zeroed out buffer behind the void pointer. This
            // works for any host, but not all plugins zero out their buffers.
            case audioMasterGetVendorString:
            case audioMasterGetProductString:
                return WantsString{};
                break;
            default:
                return DefaultDataConverter::read(opcode, index, value, data);
                break;
        }
    }

    std::optional<EventPayload> read_value(const int opcode,
                                           const intptr_t value) {
        return DefaultDataConverter::read_value(opcode, value);
    }

    void write(const int opcode, void* data, const EventResult& response) {
        switch (opcode) {
            case audioMasterGetTime:
                // Write the returned `VstTimeInfo` struct into a field and make
                // the function return a poitner to it in the function below.
                // Depending on whether the host supported the requested time
                // information this operations returns either a null pointer or
                // a pointer to a `VstTimeInfo` object.
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

    intptr_t return_value(const int opcode, const intptr_t original) {
        switch (opcode) {
            case audioMasterGetTime: {
                // Return a pointer to the `VstTimeInfo` object written in the
                // function above
                VstTimeInfo* time_info_pointer = nullptr;
                if (time_info.has_value()) {
                    time_info_pointer = &time_info.value();
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
                     const EventResult& response) {
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
