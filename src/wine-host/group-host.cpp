// yabridge: a Wine VST bridge
// Copyright (C) 2020-2021 Robbert van der Helm
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

#include "boost-fix.h"

#include <iostream>

// Generated inside of the build directory
#include <config.h>
#include <version.h>

#include "../common/utils.h"
#include "bridges/group.h"
#include "bridges/vst2.h"

/**
 * This works very similar to the host application defined in
 * `individual-host.cpp`, but instead of just loading a single plugin this will
 * act as a daemon that can host multiple 'grouped' plugins. This works by
 * allowing the `libyabridge-{vst2,vst3}.so` instance to connect this this
 * process over a socket to ask this process to host a VST `.dll` file using a
 * provided socket.  After that initialization step both the regular individual
 * plugin host and this group plugin host will function identically on both the
 * plugin and the Wine VST host side.
 */
int __attribute__((visibility("default")))
#ifdef WINE_USE_CDECL
__cdecl
#endif
    main(int argc, char* argv[]) {
    // Instead of directly hosting a plugin, this process will receive a UNIX
    // domain socket endpoint path that it should listen on to allow yabridge
    // instances to spawn plugins in this process.
    if (argc < 2) {
        std::cerr << "Usage: "
#ifdef __i386__
                  << yabridge_group_host_name_32bit
#else
                  << yabridge_group_host_name
#endif
                  << " <unix_domain_socket>" << std::endl;

        return 1;
    }

    const std::string group_socket_endpoint_path(argv[1]);

    std::cerr << "Initializing yabridge group host version "
              << yabridge_git_version
#ifdef __i386__
              << " (32-bit compatibility mode)"
#endif
              << std::endl;

    // NOTE: Some plugins use Microsoft COM, but don't initialize it first and
    //       just pray the host does it for them. Examples of this are
    //       PSPaudioware's InfiniStrip and Shattered Glass Audio Code Red Free.
    OleInitialize(nullptr);

    try {
        GroupBridge bridge(group_socket_endpoint_path);

        // Blocks the main thread until all plugins have exited
        bridge.handle_incoming_connections();
    } catch (const boost::system::system_error& error) {
        // If another process is already listening on the socket, we'll just
        // print a message and exit quietly. This could happen if the host
        // starts multiple yabridge instances that all use the same plugin group
        // at the same time.
        std::cerr << "Another process is already listening on this group's "
                     "socket, connecting to the existing process:"
                  << std::endl;
        std::cerr << error.what() << std::endl;

        return 0;
    } catch (const std::system_error& error) {
        std::cerr << "Could not create pipe:" << std::endl;
        std::cerr << error.what() << std::endl;

        return 0;
    }

    // Like in `individual-host.cpp`, this shouldn't be needed, but sometimes
    // with Wine background threads will be kept alive while this process exits
    TerminateProcess(GetCurrentProcess(), 0);
}
