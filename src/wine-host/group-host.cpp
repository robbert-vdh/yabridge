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

// Generated inside of build directory
#include <src/common/config/config.h>
#include <src/common/config/version.h>

#include "wine-bridge.h"

/**
 * This works very similar to the host application defined in
 * `individual-host.cpp`, but instead of just loading a single plugin this will
 * act as a daemon that can host multiple 'grouped' plugins. This works by
 * allowing the `libyabridge.so` instance to connect this this process over a
 * socket to ask this process to host a VST `.dll` file using a provided socket.
 * After that initialization step both the regular individual plugin host and
 * this group plugin host will function identically on both the plugin and the
 * Wine VST host side.
 *
 * The explicit calling convention is needed to work around a bug introduced in
 * Wine 5.7: https://bugs.winehq.org/show_bug.cgi?id=49138
 */
int __cdecl main(int argc, char* argv[]) {
    // Instead of directly hosting a plugin, this process will receive a UNIX
    // domain socket endpoint path that it should listen on to allow yabridge
    // instances to spawn plugins in this process.
    if (argc < 3) {
        std::cerr << "Usage: "
#ifdef __i386__
                  << yabridge_group_host_name_32bit
#else
                  << yabridge_group_host_name
#endif
                  << " <group_name> <unix_domain_socket>" << std::endl;

        return 1;
    }

    const std::string group_name(argv[1]);
    const std::string group_socket_endpoint_path(argv[2]);

    // TODO: Before doing anything, try listening on the socket and fail
    //       silently (or log a message?) if another application is already
    //       listening on the socket. This way we don't need any complicated
    //       inter-process synchronization to ensure that there is a single
    //       active group host listening for this group.
    // TODO: We should somehow try and redirect this process's STDOUT and STDERR
    //       streams to the logger so we can forward Wine's debug messages to
    //       the log even the yabridge plugin instance that initially spawned
    //       this group host process has exited. The only way I can think of
    //       doing this would be using some kind of in memory file and the
    //       `dup2()` system call.

    Logger logger = Logger::create_from_environment(group_name);
    logger.log("Initializing yabridge group host version " +
               std::string(yabridge_git_version)
#ifdef __i386__
               + " (32-bit compatibility mode)"
#endif
    );

    // TODO: After initializing, listen for connections and spawn plugins
    //       the exact same way as what happens in `individual-host.cpp`
    // TODO: Allow this process to exit when the last plugin exits. Make sure
    //       that that doesn't cause any race conditions.
}
