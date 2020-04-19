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

#include <boost/filesystem.hpp>
#include <iostream>

#include "../common/communication.h"
#include "../common/events.h"

namespace fs = boost::filesystem;

/**
 * A function pointer to what should be the entry point of a VST plugin.
 */
using VstEntryPoint = AEffect*(VST_CALL_CONV*)(audioMasterCallback);

/**
 * This ugly global is needed so we can get the instance of a `Brdige` class
 * from an `AEffect` when it performs a host callback during its initialization.
 */
PluginBridge* current_bridge_isntance = nullptr;

intptr_t VST_CALL_CONV
host_callback_proxy(AEffect*, int, int, intptr_t, void*, float);

/**
 * Fetch the Pluginbridge instance stored in one of the two pointers reserved
 * for the host of the hosted VST plugin. This is sadly needed as a workaround
 * to avoid using globals since we need free function pointers to interface with
 * the VST C API.
 */
PluginBridge& get_bridge_instance(const AEffect* plugin) {
    // This is needed during the initialization of the plugin since we can only
    // add our own pointer after it's done initializing
    if (current_bridge_isntance != nullptr) {
        // This should only be used during initialization
        assert(plugin == nullptr || plugin->ptr1 == nullptr);
        return *current_bridge_isntance;
    }

    return *static_cast<PluginBridge*>(plugin->ptr1);
}

PluginBridge::PluginBridge(std::string plugin_dll_path,
                           std::string socket_endpoint_path)
    : plugin_handle(LoadLibrary(plugin_dll_path.c_str()), &FreeLibrary),
      io_context(),
      socket_endpoint(socket_endpoint_path),
      host_vst_dispatch(io_context),
      host_vst_dispatch_midi_events(io_context),
      vst_host_callback(io_context),
      host_vst_parameters(io_context),
      host_vst_process_replacing(io_context),
      vst_host_aeffect(io_context) {
    // Got to love these C APIs
    if (plugin_handle == nullptr) {
        throw std::runtime_error("Could not load a shared library at '" +
                                 plugin_dll_path + "'.");
    }

    // VST plugin entry point functions should be called `VSTPluginMain`, but
    // there are some older deprecated names that legacy plugins may still use
    VstEntryPoint vst_entry_point = nullptr;
    for (auto name : {"VSTPluginMain", "main_plugin", "main"}) {
        vst_entry_point =
            reinterpret_cast<VstEntryPoint>(reinterpret_cast<size_t>(
                GetProcAddress(plugin_handle.get(), name)));

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
    vst_host_aeffect.connect(socket_endpoint);

    // Initialize after communication has been set up
    // We'll try to do the same `get_bridge_isntance` trick as in
    // `plugin/plugin.cpp`, but since the plugin will probably call the host
    // callback while it's initializing we sadly have to use a global here.
    current_bridge_isntance = this;
    plugin = vst_entry_point(host_callback_proxy);
    if (plugin == nullptr) {
        throw std::runtime_error("VST plugin at '" + plugin_dll_path +
                                 "' failed to initialize.");
    }

    // We only needed this little hack during initialization
    current_bridge_isntance = nullptr;
    plugin->ptr1 = this;

    // Send the plugin's information to the Linux VST plugin. Any updates during
    // runtime are handled using the `audioMasterIOChanged` host callback.
    write_object(vst_host_aeffect, *plugin);

    // This works functionally identically to the `handle_dispatch()` function
    // below, but this socket will only handle midi events. This is needed
    // because of Win32 API limitations.
    dispatch_midi_events_handler = std::thread([&]() {
        while (true) {
            passthrough_event(host_vst_dispatch_midi_events, std::nullopt,
                              plugin, plugin->dispatcher);
        }
    });

    parameters_handler = std::thread([&]() {
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
    });

    process_replacing_handler = std::thread([&]() {
        std::vector<std::vector<float>> output_buffers(plugin->numOutputs);

        while (true) {
            AudioBuffers request;
            request = read_object(host_vst_process_replacing, request,
                                  process_buffer);

            // TODO: Check if the plugin doesn't support `processReplacing` and
            //       call the legacy `process` function instead
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

            // Some plugins (e.g. Serum) don't allow audio processing while the
            // GUI is being updated
            {
                std::lock_guard lock(processing_mutex);
                plugin->processReplacing(plugin, inputs.data(), outputs.data(),
                                         request.sample_frames);
            }

            AudioBuffers response{output_buffers, request.sample_frames};
            write_object(host_vst_process_replacing, response, process_buffer);
        }
    });

    std::cout << "Finished initializing '" << plugin_dll_path << "'"
              << std::endl;
}

void PluginBridge::handle_dispatch() {
    using namespace std::placeholders;

    // For our communication we use simple threads and blocking operations
    // instead of asynchronous IO since communication has to be handled in
    // lockstep anyway
    try {
        while (true) {
            passthrough_event(host_vst_dispatch, std::nullopt, plugin,
                              std::bind(&PluginBridge::dispatch_wrapper, this,
                                        _1, _2, _3, _4, _5, _6));
        }
    } catch (const boost::system::system_error&) {
        // This happens when the sockets got closed because the plugin is being
        // shut down. In that case we can just let the whole host terminate.
        dispatch_midi_events_handler.detach();
        parameters_handler.detach();
        process_replacing_handler.detach();
    }
}

intptr_t PluginBridge::dispatch_wrapper(AEffect* plugin,
                                        int opcode,
                                        int index,
                                        intptr_t value,
                                        void* data,
                                        float option) {
    // We have to intercept GUI open calls since we can't use
    // the X11 window handle passed by the host
    switch (opcode) {
        case effEditIdle:
            // Because of the way the Win32 API works we have to process events
            // on the same thread the window was created, and that thread is the
            // thread that's handling dispatcher calls
            // To allow the GUI to update even when this thread gets blocked
            // (e.g. when a dropdown is open), the actual `effEditIdle` event
            // gets sent to the plugin on a timer.
            editor->handle_events();

            return 1;
            break;
        case effEditOpen: {
            // Create a Win32 window through Wine, embed it into the window
            // provided by the host, and let the plugin embed itself into the
            // Wine window
            const auto x11_handle = reinterpret_cast<size_t>(data);
            editor.emplace("yabridge plugin", plugin, processing_mutex,
                           x11_handle);

            return plugin->dispatcher(plugin, opcode, index, value,
                                      editor->win32_handle.get(), option);
        } break;
        case effEditClose: {
            const intptr_t return_value =
                plugin->dispatcher(plugin, opcode, index, value, data, option);

            // Cleanup is handled through RAII
            // TODO: We might want to manually unmap the X11 window instead of
            //       having the host do it. Right now the window editor window
            //       stays open for a second when removing a plugin.
            editor = std::nullopt;

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

    std::optional<EventPayload> read(const int opcode,
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
            default:
                return DefaultDataConverter::read(opcode, index, value, data);
                break;
        }
    }

    void write(const int opcode, void* data, const EventResult& response) {
        switch (opcode) {
            case audioMasterGetTime:
                // Write the returned `VstTimeInfo` struct into a field and make
                // the function return a poitner to it in the function below.
                // Depending on whether the host supported the requested time
                // information this operations returns either a null pointer or
                // a pointer to a `VstTimeInfo` object.
                if (std::holds_alternative<std::monostate>(response.payload)) {
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

   private:
    AEffect* plugin;
    std::optional<VstTimeInfo>& time_info;
};

intptr_t PluginBridge::host_callback(AEffect* effect,
                                     int opcode,
                                     int index,
                                     intptr_t value,
                                     void* data,
                                     float option) {
    HostCallbackDataConverter converter(effect, time_info);
    return send_event(vst_host_callback, host_callback_semaphore, converter,
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
