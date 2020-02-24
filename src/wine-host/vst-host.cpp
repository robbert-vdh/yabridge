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

int main(int argc, char* argv[]) {
    // We pass the name of the VST plugin .dll file to load and the Unix domain
    // socket to connect to in plugin/bridge.cpp as the first two arguments of
    // this process.
    if (argc < 3) {
        std::cerr
            << "Usage: yabridge-host.exe <vst_plugin_dll> <unix_domain_socket>"
            << std::endl;
        return 1;
    }

    const std::string plugin_dll_path(argv[1]);
    const std::string socket_endpoint_path(argv[2]);

    // I sadly could not get Boost.DLL to work here, so we'll just load the VST
    // plugisn by hand
    const auto vst_handle = LoadLibrary(plugin_dll_path.c_str());

    // TODO: Fall back to the old entry points
    const auto vst_entry_point = reinterpret_cast<VstEntryPoint>(
        reinterpret_cast<size_t>(GetProcAddress(vst_handle, "VSTPluginMain")));

    // TODO: Check whether this returned a null pointer
    AEffect* plugin = vst_entry_point(host_callback);

    // Connect to the sockets for communication once the plugin has finished
    // loading
    // TODO: The program should terminate gracefully when one of the sockets
    //       gets closed
    // TODO: Remove debug and move most of these things to
    //       `wine-host/bridge.cpp`, similar to `plugin/bridge.cpp`

    boost::asio::io_context io_context;
    boost::asio::local::stream_protocol::endpoint socket_endpoint(
        socket_endpoint_path);

    // The naming convention for these sockets is `<from>_<to>_<event>`. For
    // instance the socket named `host_vst_dispatch` forwards
    // `AEffect.dispatch()` calls from the native VST host to the Windows VST
    // plugin (through the Wine VST host).
    boost::asio::local::stream_protocol::socket host_vst_dispatch(io_context);
    host_vst_dispatch.connect(socket_endpoint);

    // TODO: Remove debug, we're just reporting the plugin's name we retrieved
    //       above
    std::array<char, max_string_size> buffer;
    while (true) {
        auto event = read_object<Event>(host_vst_dispatch);

        // The void pointer argument for the dispatch function is used for
        // either:
        //  - Not at all, in which case it will be a null pointer
        //  - For passing strings as input to the event
        //  - For providing a buffer for the event to write results back into
        char* payload = nullptr;
        if (event.data.has_value()) {
            // If the data parameter was an empty string, then we're going to
            // pass a larger buffer to the dispatch function instead..
            if (!event.data->empty()) {
                payload = const_cast<char*>(event.data->c_str());
            } else {
                payload = buffer.data();
            }
        }

        const intptr_t return_value =
            plugin->dispatcher(plugin, event.opcode, event.option,
                               event.parameter, payload, event.option);

        // Only write back the value from `payload` if we were passed an empty
        // buffer to write into
        bool is_updated = event.data.has_value() && event.data->empty();

        if (is_updated) {
            EventResult response{return_value, payload};
            write_object(host_vst_dispatch, response);
        } else {
            EventResult response{return_value, std::nullopt};
            write_object(host_vst_dispatch, response);
        }
    }
}

// TODO: Placeholder
intptr_t VST_CALL_CONV host_callback(AEffect* plugin,
                                     int32_t opcode,
                                     int32_t parameter,
                                     intptr_t value,
                                     void* data,
                                     float option) {
    return 1;
}
