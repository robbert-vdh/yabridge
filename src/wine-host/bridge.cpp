#include "bridge.h"

#include "../common/communication.h"

/**
 * A function pointer to what should be the entry point of a VST plugin.
 */
using VstEntryPoint = AEffect*(VST_CALL_CONV*)(audioMasterCallback);

/**
 * This ugly global is needed so we can get the instance of a `Brdige` class
 * from an `AEffect` when it performs a host callback during its initialization.
 */
Bridge* current_bridge_isntance = nullptr;

intptr_t VST_CALL_CONV
host_callback_proxy(AEffect*, int32_t, int32_t, intptr_t, void*, float);

/**
 * Fetch the bridge instance stored in one of the two pointers reserved for the
 * host of the hosted VST plugin. This is sadly needed as a workaround to avoid
 * using globals since we need free function pointers to interface with the VST
 * C API.
 */
Bridge& get_bridge_instance(const AEffect* plugin) {
    // This is needed during the initialization of the plugin since we can only
    // add our own pointer after it's done initializing
    if (current_bridge_isntance != nullptr) {
        // This should only be used during initialization
        assert(plugin == nullptr || plugin->ptr1 == nullptr);
        return *current_bridge_isntance;
    }

    return *static_cast<Bridge*>(plugin->ptr1);
}

Bridge::Bridge(std::string plugin_dll_path, std::string socket_endpoint_path)
    : plugin_handle(LoadLibrary(plugin_dll_path.c_str()), &FreeLibrary),
      io_context(),
      socket_endpoint(socket_endpoint_path),
      host_vst_dispatch(io_context),
      vst_host_callback(io_context) {
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

    // We only needed this little hack during initialization
    current_bridge_isntance = nullptr;
    plugin->ptr1 = this;
}

// TODO: Replace blocking loop with async readers or threads for all of the
//       sockets. Also extract this functionality somewhere since the host event
//       callback needs to do exactly the same thing.
void Bridge::dispatch_loop() {
    while (true) {
        passthrough_event(host_vst_dispatch, plugin, plugin->dispatcher);
    }
}

intptr_t Bridge::host_callback(AEffect* /*plugin*/,
                               int32_t opcode,
                               int32_t index,
                               intptr_t value,
                               void* data,
                               float option) {
    return send_event(vst_host_callback, opcode, index, value, data, option);
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
