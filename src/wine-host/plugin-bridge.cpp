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
host_callback_proxy(AEffect*, int32_t, int32_t, intptr_t, void*, float);

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
      process_buffer(std::make_unique<AudioBuffers::buffer_type>()) {
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

    // Send the plugin's information to the Linux VST plugin
    // TODO: This is now done only once at startup, do plugins update their
    //       parameters? In that case we should be detecting updates and pass
    //       them along accordingly.
    write_object(vst_host_aeffect, *plugin);

    // We only needed this little hack during initialization
    current_bridge_isntance = nullptr;
    plugin->ptr1 = this;

    // For our communication we use simple threads and blocking operations
    // instead of asynchronous IO since communication has to be handled in
    // lockstep anyway
    dispatch_handler = std::thread([&]() {
        while (true) {
            passthrough_event(host_vst_dispatch, plugin, plugin->dispatcher,
                              std::nullopt);
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
                                  *process_buffer);

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

            plugin->process(plugin, inputs.data(), outputs.data(),
                            request.sample_frames);

            AudioBuffers response{output_buffers, request.sample_frames};
            write_object(host_vst_process_replacing, response, *process_buffer);
        }
    });

    std::cout << "Finished initializing '" << plugin_dll_path << "'";
}

void PluginBridge::wait() {
    dispatch_handler.join();
    parameters_handler.join();
    process_replacing_handler.join();
}

intptr_t PluginBridge::host_callback(AEffect* /*plugin*/,
                                     int32_t opcode,
                                     int32_t index,
                                     intptr_t value,
                                     void* data,
                                     float option) {
    return send_event(vst_host_callback, opcode, index, value, data, option,
                      std::nullopt);
}

intptr_t VST_CALL_CONV host_callback_proxy(AEffect* effect,
                                           int32_t opcode,
                                           int32_t index,
                                           intptr_t value,
                                           void* data,
                                           float option) {
    return get_bridge_instance(effect).host_callback(effect, opcode, index,
                                                     value, data, option);
}
