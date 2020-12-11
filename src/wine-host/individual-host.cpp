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
#include <thread>

// Generated inside of the build directory
#include <src/common/config/config.h>
#include <src/common/config/version.h>

#include "../common/utils.h"
#include "bridges/vst2.h"

/**
 * This is the default VST host application. It will load the specified VST2
 * plugin, and then connect back to the `libyabridge.so` instance that spawned
 * this over the socket.
 *
 * The explicit calling convention is needed to work around a bug introduced in
 * Wine 5.7: https://bugs.winehq.org/show_bug.cgi?id=49138
 */
int __cdecl __attribute__((visibility("default")))
main(int argc, char* argv[]) {
    set_realtime_priority();

    // We pass the name of the VST plugin .dll file to load and the base
    // directory for the Unix domain socket endpoints to connect to as the first
    // two arguments of this process in plugin/bridge.cpp.
    if (argc < 3) {
        std::cerr << "Usage: "
#ifdef __i386__
                  << yabridge_individual_host_name_32bit
#else
                  << yabridge_individual_host_name
#endif
                  << " <vst_plugin_dll> <endpoint_base_directory>" << std::endl;

        return 1;
    }

    const std::string plugin_dll_path(argv[1]);
    const std::string socket_endpoint_path(argv[2]);

    std::cout << "Initializing yabridge host version " << yabridge_git_version
#ifdef __i386__
              << " (32-bit compatibility mode)"
#endif
              << std::endl;

    // As explained in `Vst2Bridge`, the plugin has to be initialized in the
    // same thread as the one that calls `io_context.run()`. This setup is
    // slightly more convoluted than it has to be, but doing it this way we
    // don't need to differentiate between individually hosted plugins and
    // plugin groups when it comes to event handling.
    MainContext main_context{};
    std::unique_ptr<Vst2Bridge> bridge;
    try {
        bridge = std::make_unique<Vst2Bridge>(main_context, plugin_dll_path,
                                              socket_endpoint_path);
    } catch (const std::runtime_error& error) {
        std::cerr << "Error while initializing Wine VST host:" << std::endl;
        std::cerr << error.what() << std::endl;

        return 1;
    }

    std::cout << "Finished initializing '" << plugin_dll_path << "'"
              << std::endl;

    // We'll listen for `dispatcher()` calls on a different thread, but the
    // actual events will still be executed within the IO context
    Win32Thread dispatch_handler([&]() {
        bridge->handle_dispatch();

        // // When the sockets get closed, this application should
        // // terminate gracefully
        // main_context.stop();
        // FIXME: So some of the background threads spawned by the plugin may
        //        get stuck if the host got terminated abruptly. After an entire
        //        day of debugging I still have no idea whether this is a bug in
        //        yabridge, Wine, or those plugins, but just killing off this
        //        process and all of its threads 'fixes' the issue. #69
        TerminateProcess(GetCurrentProcess(), 0);
    });

    // Handle Win32 messages and X11 events on a timer, just like in
    // `GroupBridge::async_handle_events()``
    main_context.async_handle_events([&]() {
        bridge->handle_x11_events();
        bridge->handle_win32_events();
    });
    main_context.run();
}
