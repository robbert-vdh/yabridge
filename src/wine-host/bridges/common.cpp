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

#include "common.h"

#include <iostream>

#include "../editor.h"

HostBridge::HostBridge(MainContext& main_context,
                       boost::filesystem::path plugin_path,
                       pid_t parent_pid)
    : plugin_path(plugin_path),
      main_context(main_context),
      generic_logger(Logger::create_wine_stderr()),
      parent_pid(parent_pid),
      watchdog_guard(main_context.register_watchdog(*this)) {}

HostBridge::~HostBridge() noexcept {}

void HostBridge::handle_events() noexcept {
    MSG msg;

    for (int i = 0;
         i < max_win32_messages && PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE);
         i++) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void HostBridge::shutdown_if_dangling() {
    // If the parent process has exited and this plugin bridge instance is
    // outliving the process it's supposed to be connected to (because in some
    // situations sockets won't get closed when this happens so we'd hang on
    // `recv()`), then we'll close the sockets here so that the plugin bridge
    // exits gracefully. This will be periodically called from `MainContext`'s
    // watchdog thread.
    if (!pid_running(parent_pid)) {
        std::cerr << "WARNING: The native plugin host seems to have died."
                  << std::endl;
        std::cerr << "         This bridge will shut down now." << std::endl;

        // FIXME: Closing the sockets should work fine, but it still leaves some
        //        background threads hanging around. For now we'll just
        //        terminate the entire process instead since we'll probably be
        //        left in a bad state anyways. The only thing this could
        //        potentially break would be sharing a plugin group across two
        //        different DAWs, but you really shouldn't be doing that. :D
        //
        //        Check this commit for another now-unnecessary change we
        //        reverted here.
        // close_sockets();
        TerminateProcess(GetCurrentProcess(), 0);
    }
}
