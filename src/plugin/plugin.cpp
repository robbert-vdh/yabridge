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

#include <vestige/aeffectx.h>

#include <iostream>
#include <memory>

#include "../common/logging.h"
#include "bridges/vst2.h"

#define VST_EXPORT __attribute__((visibility("default")))

// The main entry point for VST plugins should be called `VSTPluginMain``. The
// other one exist for legacy reasons since some old hosts might still use them.
// There's also another possible legacy entry point just called `main`, but GCC
// will refuse to compile a function called `main` that's not a regular C++ main
// function

/**
 * The main VST plugin entry point. We first set up a bridge that connects to a
 * Wine process that hosts the Windows VST plugin. We then create and return a
 * VST plugin struct that acts as a passthrough to the bridge.
 *
 * To keep this somewhat contained this is the only place where we're doing
 * manual memory management. Clean up is done when we receive the `effClose`
 * opcode from the VST host (i.e. opcode 1).`
 */
extern "C" VST_EXPORT AEffect* VSTPluginMain(
    audioMasterCallback host_callback) {
    try {
        // This is the only place where we have to use manual memory management.
        // The bridge's destructor is called when the `effClose` opcode is
        // received.
        Vst2PluginBridge* bridge = new Vst2PluginBridge(host_callback);

        return &bridge->plugin;
    } catch (const std::exception& error) {
        Logger logger = Logger::create_from_environment();
        logger.log("Error during initialization:");
        logger.log(error.what());

        return nullptr;
    }
}

extern "C" VST_EXPORT AEffect* main_plugin(audioMasterCallback audioMaster) {
    return VSTPluginMain(audioMaster);
}
