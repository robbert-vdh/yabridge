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
#include <src/common/config/version.h>

#include "plugin-bridge.h"

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

    std::cerr << "Initializing yabridge host version " << yabridge_git_version
              << std::endl;
    try {
        PluginBridge bridge(plugin_dll_path, socket_endpoint_path);
        std::cerr << "Finished initializing '" << plugin_dll_path << "'"
                  << std::endl;

        // Blocks the main thread until the plugin shuts down
        bridge.handle_dispatch();
    } catch (const std::runtime_error& error) {
        std::cerr << "Error while initializing Wine VST host:" << std::endl;
        std::cerr << error.what() << std::endl;

        return 1;
    }
}
