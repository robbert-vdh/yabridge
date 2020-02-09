#include <iostream>

#include "native-includes.h"

// `native-includes.h` has to be included before any other files as otherwise we
// might get the wrong version of certain libraries
#define WIN32_LEAN_AND_MEAN
#include <vestige/aeffect.h>
#include <windows.h>

#include "../common/communication.h"

/**
 * A function pointer to what should be the entry point of a VST plugin.
 */
using VstEntryPoint = AEffect*(VST_CALL_CONV*)(audioMasterCallback);

intptr_t VST_CALL_CONV
host_callback(AEffect*, int32_t, int32_t, intptr_t, void*, float);

int main() {
    // TODO: We're going to need to forward messages both from the host to the
    //       plugin and from the plugin to the host. It might be useful to use
    //       two sockets here so both channels can be handled independently.

    // TODO: Load the right VST plugin
    // I sadly could not get Boost.DLL to work here, so we'll just load the VST
    // plugisn by hand
    const auto vst_handle = LoadLibrary(
        "/home/robbert/.wine/drive_c/Program "
        "Files/Steinberg/VstPlugins/Serum_x64.dll");

    // TODO: Fall back to the old entry points
    const auto vst_entry_point = reinterpret_cast<VstEntryPoint>(
        GetProcAddress(vst_handle, "VSTPluginMain"));

    // TODO: Check whether this returned a null pointer
    AEffect* plugin = vst_entry_point(host_callback);

    // TODO: Check the spec, how large should this be?
    std::vector<char> buffer(2048, 0);
    plugin->dispatcher(plugin, effGetEffectName, 0, 0, buffer.data(), 0.0);

    std::string plugin_title(buffer.data());

    // TODO: The program should terminate automatically when stdin gets closed

    // TODO: Remove debug, we're just reporting the plugin's name we retrieved
    //       above
    while (true) {
        auto event = read_object<Event>(std::cin);

        EventResult response;
        if (event.opcode == effGetEffectName) {
            response.result = plugin_title;
            response.return_value = 1;
        } else {
            response.return_value = 0;
        }

        write_object(std::cout, response);
    }
}

// TODO: Placeholder
intptr_t VST_CALL_CONV host_callback(AEffect* plugin,
                                     int32_t opcode,
                                     int32_t parameter,
                                     intptr_t value,
                                     void* result,
                                     float option) {
    return 1;
}
