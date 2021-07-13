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

#include <vestige/aeffectx.h>

#include <iostream>
#include <memory>

#include "../common/logging/common.h"
#include "bridges/vst2.h"

using namespace std::literals::string_literals;

#define VST_EXPORT __attribute__((visibility("default")))

// The main entry point for VST2 plugins should be called `VSTPluginMain``. The
// other one exist for legacy reasons since some old hosts might still use them
// (EnergyXT being the only known host on Linux that uses the `main` entry
// point).

/**
 * The main VST2 plugin entry point. We first set up a bridge that connects to a
 * Wine process that hosts the Windows VST2 plugin. We then create and return a
 * VST plugin struct that acts as a passthrough to the bridge.
 *
 * To keep this somewhat contained this is the only place where we're doing
 * manual memory management. Clean up is done when we receive the `effClose`
 * opcode from the VST2 host (i.e. opcode 1).`
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

        // Also show a desktop notification most people likely won't see the
        // above message
        send_notification(
            "Failed to initialize VST2 plugin",
            error.what() +
                "\nIf you just updated yabridge, then you may need to rerun "
                "'yabridgectl sync' first to update your plugins."s,
            true);

        return nullptr;
    }
}

// XXX: GCC doens't seem to have a clean way to let you define an arbitrary
//      function called 'main'. Even JUCE does it this way, so it should be
//      safe.
extern "C" VST_EXPORT AEffect* deprecated_main(
    audioMasterCallback audioMaster) asm("main");
VST_EXPORT AEffect* deprecated_main(audioMasterCallback audioMaster) {
    return VSTPluginMain(audioMaster);
}
