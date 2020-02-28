#include "bridge.h"

/**
 * A function pointer to what should be the entry point of a VST plugin.
 */
using VstEntryPoint = AEffect*(VST_CALL_CONV*)(audioMasterCallback);

intptr_t VST_CALL_CONV
host_callback(AEffect*, int32_t, int32_t, intptr_t, void*, float);

Bridge::Bridge(std::string plugin_dll_path, std::string socket_endpoint_path)
    : plugin_handle(LoadLibrary(plugin_dll_path.c_str()), &FreeLibrary),
      io_context(),
      socket_endpoint(socket_endpoint_path),
      host_vst_dispatch(io_context) {
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

    plugin = vst_entry_point(host_callback);
    if (plugin == nullptr) {
        throw std::runtime_error("VST plugin at '" + plugin_dll_path +
                                 "' failed to initialize.");
    }

    host_vst_dispatch.connect(socket_endpoint);
}

// // TODO: Placeholder
// intptr_t VST_CALL_CONV host_callback(AEffect* plugin,
//                                      int32_t opcode,
//                                      int32_t index,
//                                      intptr_t value,
//                                      void* data,
//                                      float option) {
//     return 1;
// }
