// yabridge: a Wine plugin bridge
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

#include "bridges/vst3.h"

using namespace std::literals::string_literals;

// FIXME: The VST3 SDK as of version 3.7.2 now includes multiple local functions
//        called `InitModule` and `DeinitModule`: one in the new
//        `public.sdk/source/main/initmodule.cpp`, and the existing ones in the
//        OS-specific `*main.cpp` files. These cause linking errors during unity
//        builds, so we'll just rename the ones in this include so we can stay
//        as close to the vanilla SDK as possible.
#define InitModule init_module
#define DeinitModule deinit_module
// NOLINTNEXTLINE(bugprone-suspicious-include)
#include <public.sdk/source/main/linuxmain.cpp>

using namespace std::literals::string_literals;

namespace fs = ghc::filesystem;

// Because VST3 plugins consist of completely independent components that have
// to be initialized and connected by the host, hosting a VST3 plugin through
// yabridge works very differently from hosting VST2 plugin. Even with
// individually hosted plugins, all instances of the plugin will be handled by a
// single dynamic library (that VST3 calls a 'module'). Because of this, we'll
// spawn our host process when the first instance of a plugin gets initialized,
// and when the last instance exits so will the host process.
//
// Even though the new VST3 module format where everything's inside of a bundle
// is not particularly common, it is the only standard for Linux and that's what
// we'll use. The installation format for yabridge will thus have the Windows
// plugin symlinked to either the `x86_64-win` or the `x86-win` directory inside
// of the bundle, even if it does not come in a bundle itself.

std::unique_ptr<Vst3PluginBridge> bridge;

bool InitModule() {
    assert(!bridge);

    // FIXME: Update this for the chainloading
    const fs::path plugin_path = get_this_file_location();

    try {
        bridge = std::make_unique<Vst3PluginBridge>(plugin_path);

        return true;
    } catch (const std::exception& error) {
        Logger logger = Logger::create_exception_logger();

        logger.log("");
        logger.log("Error during initialization:");
        logger.log(error.what());
        logger.log("");

        // Also show a desktop notification most people likely won't see the
        // above message
        send_notification(
            "Failed to initialize VST3 plugin",
            error.what() +
                "\nIf you just updated yabridge, then you may need to rerun "
                "'yabridgectl sync' first to update your plugins."s,
            plugin_path);

        return false;
    }
}

bool DeinitModule() {
    assert(bridge);

    bridge.reset();

    return true;
}

/**
 * Our VST3 plugin's entry point. When building the plugin factory we'll host
 * the plugin in our Wine application, retrieve its information and supported
 * classes, and then recreate it here.
 */
YABRIDGE_EXPORT Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory() {
    // The host should have called `InitModule()` first
    assert(bridge);

    return bridge->get_plugin_factory();
}
