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

#include "vst3.h"

#include "../boost-fix.h"

#include <public.sdk/source/vst/hosting/module_win32.cpp>

Vst3Bridge::Vst3Bridge(MainContext& main_context,
                       std::string plugin_dll_path,
                       std::string endpoint_base_dir)
    : HostBridge(plugin_dll_path),
      main_context(main_context),
      sockets(main_context.context, endpoint_base_dir, false) {
    std::string error;
    module = VST3::Hosting::Win32Module::create(plugin_dll_path, error);

    // TODO: Do something more useful with this
    if (module) {
        // TODO: They use some thin wrappers around the interfaces, we can
        //       probably reuse these instead of having to make our own
        VST3::Hosting::FactoryInfo info = module->getFactory().info();
        std::cout << "Plugin name:  " << module->getName() << std::endl;
        std::cout << "Vendor:       " << info.vendor() << std::endl;
        std::cout << "URL:          " << info.url() << std::endl;
        std::cout << "Send spam to: " << info.email() << std::endl;
    } else {
        throw std::runtime_error("Could not load the VST3 module for '" +
                                 plugin_dll_path + "': " + error);
    }

    sockets.connect();

    // TODO: We should send a copy of the configuration from the plugin at this
    // point config = sockets.host_vst_control.receive_single<Configuration>();

    control_handler = Win32Thread([&]() {
        // TODO: Handle control messages
        // sockets.host_vst_control.receive_multi();
    });
}

void Vst3Bridge::run() {
    // TODO: Do something
    std::cerr << "TODO: Not yet implemented" << std::endl;
}
