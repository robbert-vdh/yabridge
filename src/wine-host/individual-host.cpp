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
 * This is the default plugin host application. It will load the specified
 * plugin plugin, and then connect back to the `libyabridge-{vst2,vst3}.so`
 * instance that spawned this over the socket.
 *
 * The explicit calling convention is needed to work around a bug introduced in
 * Wine 5.7: https://bugs.winehq.org/show_bug.cgi?id=49138
 */
int __cdecl __attribute__((visibility("default")))
main(int argc, char* argv[]) {
    set_realtime_priority();

    // We pass plugin format, the name of the VST2 plugin .dll file or VST3
    // bundle to load, and the base directory for the Unix domain socket
    // endpoints to connect to as the first two arguments of this process in
    // `src/plugin/host-process.cpp`
    if (argc < 4) {
        std::cerr
            << "Usage: "
#ifdef __i386__
            << yabridge_individual_host_name_32bit
#else
            << yabridge_individual_host_name
#endif
            << " <plugin_type> <plugin_location> <endpoint_base_directory>"
            << std::endl;

        return 1;
    }

    // TODO: On the Wine side of things, we should only allow hosting VST3
    //       plugins when the Meson build option is enabled (because, well,
    //       otherwise we'd get compile errors)
    const std::string plugin_type_str(argv[1]);
    const PluginType plugin_type = plugin_type_from_string(plugin_type_str);
    const std::string plugin_location(argv[2]);
    const std::string socket_endpoint_path(argv[3]);

    std::cout << "Initializing yabridge host version " << yabridge_git_version
#ifdef __i386__
              << " (32-bit compatibility mode)"
#endif
              << std::endl;
    std::cout << "Preparing to load " << plugin_type_to_string(plugin_type)
              << " plugin at '" << plugin_location << "'" << std::endl;

    // As explained in `Vst2Bridge`, the plugin has to be initialized in the
    // same thread as the one that calls `io_context.run()`. This setup is
    // slightly more convoluted than it has to be, but doing it this way we
    // don't need to differentiate between individually hosted plugins and
    // plugin groups when it comes to event handling.
    MainContext main_context{};
    Win32Thread worker_thread;
    std::shared_ptr<HostBridge> bridge;
    try {
        switch (plugin_type) {
            case PluginType::vst2:
                bridge = std::make_shared<Vst2Bridge>(
                    main_context, plugin_location, socket_endpoint_path);

                // We'll listen for `dispatcher()` calls on a different thread,
                // but the actual events will still be executed within the IO
                // context
                worker_thread = Win32Thread([&]() {
                    std::static_pointer_cast<Vst2Bridge>(bridge)
                        ->handle_dispatch();

                    // When the sockets get closed, this application should
                    // terminate gracefully
                    main_context.stop();
                });
                break;
            case PluginType::vst3:
                std::cerr << "TODO: Not yet implemented" << std::endl;
                return 1;
                break;
            case PluginType::unknown:
                std::cerr << "Unknown plugin type '" << plugin_type_str << "'"
                          << std::endl;
                return 1;
                break;
        };
    } catch (const std::runtime_error& error) {
        std::cerr << "Error while initializing the Wine plugin host:"
                  << std::endl;
        std::cerr << error.what() << std::endl;

        return 1;
    }

    std::cout << "Finished initializing '" << plugin_location << "'"
              << std::endl;

    // Handle Win32 messages and X11 events on a timer, just like in
    // `GroupBridge::async_handle_events()``
    main_context.async_handle_events([&]() {
        bridge->handle_x11_events();
        bridge->handle_win32_events();
    });
    main_context.run();
}
