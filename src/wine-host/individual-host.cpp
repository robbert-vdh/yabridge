// yabridge: a Wine VST bridge
// Copyright (C) 2020-2022 Robbert van der Helm
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
#include <config.h>
#include <version.h>

#include "../common/utils.h"
#include "bridges/vst2.h"
#ifdef WITH_VST3
#include "bridges/vst3.h"
#endif

static const std::string host_name = "yabridge host version " +
                                     std::string(yabridge_git_version)
#ifdef __i386__
                                     + " (32-bit compatibility mode)"
#endif
    ;

/**
 * This is the default plugin host application. It will load the specified
 * plugin plugin, and then connect back to the `libyabridge-{vst2,vst3}.so`
 * instance that spawned this over the socket.
 */
int __attribute__((visibility("default")))
#ifdef WINE_USE_CDECL
__cdecl
#endif
    main(int argc, char* argv[]) {
    // We pass the plugin format, the name of the VST2 plugin .dll file or VST3
    // bundle to load, the base directory for the Unix domain socket endpoints
    // to connect to and the process ID of the process the native plugin is
    // being hosted in as arguments for yabridge-host.exe
    if (argc < 5) {
        std::cerr << host_name << std::endl;
        std::cerr << "Usage: "
#ifdef __i386__
                  << yabridge_individual_host_name_32bit
#else
                  << yabridge_individual_host_name
#endif
                  << " <plugin_type> <plugin_location> "
                     "<endpoint_base_directory> <parent_pid>"
                  << std::endl;

        return 1;
    }

    const std::string plugin_type_str(argv[1]);
    const PluginType plugin_type = plugin_type_from_string(plugin_type_str);
    const std::string plugin_location(argv[2]);
    const std::string socket_endpoint_path(argv[3]);
    const pid_t parent_pid = std::stoi(argv[4]);

    std::cerr << "Initializing " << host_name << std::endl;
    std::cerr << "Preparing to load " << plugin_type_to_string(plugin_type)
              << " plugin at '" << plugin_location << "'" << std::endl;

    // NOTE: Some plugins use Microsoft COM, but don't initialize it first and
    //       just pray the host does it for them. Examples of this are
    //       PSPaudioware's InfiniStrip and Shattered Glass Audio Code Red Free.
    OleInitialize(nullptr);

    // As explained in `Vst2Bridge`, the plugin has to be initialized in the
    // same thread as the one that calls `io_context.run()`. This setup is
    // slightly more convoluted than it has to be, but doing it this way we
    // don't need to differentiate between individually hosted plugins and
    // plugin groups when it comes to event handling.
    MainContext main_context{};
    std::unique_ptr<HostBridge> bridge;
    try {
        switch (plugin_type) {
            case PluginType::vst2:
                bridge = std::make_unique<Vst2Bridge>(
                    main_context, plugin_location, socket_endpoint_path,
                    parent_pid);
                break;
            case PluginType::vst3:
#ifdef WITH_VST3
                bridge = std::make_unique<Vst3Bridge>(
                    main_context, plugin_location, socket_endpoint_path,
                    parent_pid);
#else
                std::cerr << "This version of yabridge has not been compiled "
                             "with VST3 support"
                          << std::endl;
                return 1;
#endif
                break;
            case PluginType::unknown:
                std::cerr << "Unknown plugin type '" << plugin_type_str << "'"
                          << std::endl;
                return 1;
                break;
        };
    } catch (const std::exception& error) {
        std::cerr << "Error while initializing the Wine plugin host:"
                  << std::endl;
        std::cerr << error.what() << std::endl;

        // See below, just returning from `main()` isn't enough to terminate the
        // process
        TerminateProcess(GetCurrentProcess(), 0);

        return 1;
    }

    // Let the plugin receive and handle its events on its own thread. Some
    // potentially unsafe events that should always be run from the UI thread
    // will be posted to `main_context`.
    Win32Thread worker_thread([&]() {
        pthread_setname_np(pthread_self(), "worker");

        bridge->run();

        // // When the sockets get closed, this application should
        // // terminate gracefully
        // main_context.stop();
        // FIXME: So some of the background threads spawned by the plugin may
        //        get stuck if the host got terminated abruptly. After an entire
        //        day of debugging I still have no idea whether this is a bug in
        //        yabridge, Wine, or those plugins, but just killing off this
        //        process and all of its threads 'fixes' the issue.
        //
        //        https://github.com/robbert-vdh/yabridge/issues/69
        TerminateProcess(GetCurrentProcess(), 0);
    });

    std::cerr << "Finished initializing '" << plugin_location << "'"
              << std::endl;

    // Handle Win32 messages and X11 events on a timer, just like in
    // `GroupBridge::async_handle_events()``
    main_context.async_handle_events(
        [&]() { bridge->handle_events(); },
        [&]() { return !bridge->inhibits_event_loop(); });
    main_context.run();
}
