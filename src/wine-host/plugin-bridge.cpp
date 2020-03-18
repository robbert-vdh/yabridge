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
      vst_host_callback(io_context),
      host_vst_parameters(io_context),
      host_vst_process_replacing(io_context),
      vst_host_aeffect(io_context),
      editor("yabridge plugin") {
    // Got to love these C APIs
    if (plugin_handle == nullptr) {
        throw std::runtime_error("Could not load a shared library at '" +
                                 plugin_dll_path + "'.");
    }

    // VST plugin entry point functions should be called `VSTPluginMain`, but
    // there are some older deprecated names that legacy plugins may still use
    VstEntryPoint vst_entry_point = nullptr;
    for (auto name : {"VSTPluginMain", "MAIN", "main_plugin"}) {
        vst_entry_point =
            reinterpret_cast<VstEntryPoint>(reinterpret_cast<size_t>(
                GetProcAddress(plugin_handle.get(), name)));

        if (name != nullptr) {
            break;
        }
    }
    if (vst_entry_point == nullptr) {
        throw std::runtime_error(
            "Could not find a valid VST entry point for '" + plugin_dll_path +
            "'.");
    }

    // It's very important that these sockets are accepted to in the same order
    // in the Linus plugin
    host_vst_dispatch.connect(socket_endpoint);
    vst_host_callback.connect(socket_endpoint);
    host_vst_parameters.connect(socket_endpoint);
    host_vst_process_replacing.connect(socket_endpoint);
    vst_host_aeffect.connect(socket_endpoint);

    // Initialize after communication has been set up We'll try to do the same
    // `get_bridge_isntance` trick as in `plugin/plugin.cpp`, but since the
    // plugin will probably call the host callback while it's initializing we
    // sadly have to use a global here.
    current_bridge_isntance = this;
    plugin = vst_entry_point(host_callback_proxy);
    if (plugin == nullptr) {
        throw std::runtime_error("VST plugin at '" + plugin_dll_path +
                                 "' failed to initialize.");
    }

    // Send the plugin's information to the Linux VST plugin. Any updates during
    // runtime are handled using the `audioMasterIOChanged` host callback.
    write_object(vst_host_aeffect, *plugin);

    // We only needed this little hack during initialization
    current_bridge_isntance = nullptr;
    plugin->ptr1 = this;

    // For our communication we use simple threads and blocking operations
    // instead of asynchronous IO since communication has to be handled in
    // lockstep anyway
    dispatch_handler = std::thread([&]() {
        using namespace std::placeholders;

        while (true) {
            passthrough_event(host_vst_dispatch, std::nullopt, plugin,
                              std::bind(&PluginBridge::dispatch_wrapper, this,
                                        _1, _2, _3, _4, _5, _6));
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
        while (true) {
            AudioBuffers request;
            request = read_object(host_vst_process_replacing, request,
                                  process_buffer);

            // TODO: Check if the plugin doesn't support `processReplacing` and
            //       call the legacy `process` function instead
            std::vector<std::vector<float>> output_buffers(
                plugin->numOutputs, std::vector<float>(request.sample_frames));

            // The process functions expect a `float**` for their inputs and
            // their outputs
            std::vector<float*> inputs;
            for (auto& buffer : request.buffers) {
                inputs.push_back(buffer.data());
            }
            std::vector<float*> outputs;
            for (auto& buffer : output_buffers) {
                outputs.push_back(buffer.data());
            }

            plugin->processReplacing(plugin, inputs.data(), outputs.data(),
                                     request.sample_frames);

            AudioBuffers response{output_buffers, request.sample_frames};
            write_object(host_vst_process_replacing, response, process_buffer);
        }
    });

    std::cout << "Finished initializing '" << plugin_dll_path << "'"
              << std::endl;
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
        case effEditOpen: {
            const auto win32_handle = editor.open();

            // The plugin will return 0 if it can not open its
            // editor window (or if it does not support it, but in
            // that case the DAW should be hiding the option)
            const intptr_t return_value = plugin->dispatcher(
                plugin, opcode, index, value, win32_handle, option);
            if (return_value == 0) {
                return 0;
            }

            const auto x11_handle = reinterpret_cast<size_t>(data);
            editor.embed_into(x11_handle);

            return return_value;
            break;
        }
        case effEditClose: {
            const intptr_t return_value =
                plugin->dispatcher(plugin, opcode, index, value, data, option);

            editor.close();

            return return_value;
            break;
        }
        default:
            return plugin->dispatcher(plugin, opcode, index, value, data,
                                      option);
            break;
    }
}

void PluginBridge::wait() {
    dispatch_handler.join();
    parameters_handler.join();
    process_replacing_handler.join();
}

class HostCallbackDataConverter : DefaultDataConverter {
   public:
    HostCallbackDataConverter(AEffect* plugin, VstTimeInfo& time_info)
        : plugin(plugin), time_info(time_info) {}

    std::optional<EventPayload> read(const int opcode,
                                     const intptr_t value,
                                     const void* data) {
        switch (opcode) {
            // Some hsots will outright crash if they receive this opcode, not
            // sure why they don't just ignore it. Please let me know if there's
            // a better way to handle this instead of just ignoring the event!
            //
            // TODO: Filtering these two events fixes crashes, but should this
            //       be needed? `audioMasterWantMidi` is deprecated though.
            case audioMasterWantMidi:
            case audioMasterUpdateDisplay:
                std::cerr << "Got opcode "
                          << opcode_to_string(false, opcode)
                                 .value_or(std::to_string(opcode))
                          << "), ignoring..." << std::endl;

                return std::nullopt;
                break;
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
                return DefaultDataConverter::read(opcode, value, data);
                break;
        }
    }

    void write(const int opcode, void* data, const EventResult& response) {
        switch (opcode) {
            case audioMasterGetTime:
                // Write the returned `VstTimeInfo` struct into a field and make
                // the function return a poitner to it in the function below
                time_info = std::get<VstTimeInfo>(response.payload);
                break;
            default:
                DefaultDataConverter::write(opcode, data, response);
                break;
        }
    }

    intptr_t return_value(const int opcode, const intptr_t original) {
        switch (opcode) {
            case audioMasterGetTime:
                // Return a pointer to the `VstTimeInfo` object written in the
                // function above
                return reinterpret_cast<intptr_t>(&time);
                break;
            default:
                return DefaultDataConverter::return_value(opcode, original);
                break;
        }
    }

   private:
    AEffect* plugin;
    VstTimeInfo& time_info;
};

intptr_t PluginBridge::host_callback(AEffect* /*plugin*/,
                                     int opcode,
                                     int index,
                                     intptr_t value,
                                     void* data,
                                     float option) {
    HostCallbackDataConverter converter(plugin, time_info);
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
