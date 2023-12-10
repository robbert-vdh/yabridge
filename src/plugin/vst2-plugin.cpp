// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2023 Robbert van der Helm
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

// Generated inside of the build directory
#include <version.h>

#include "../common/linking.h"
#include "bridges/vst2.h"

using namespace std::literals::string_literals;

namespace fs = ghc::filesystem;

// The main entry point for VST2 plugins should be called `VSTPluginMain``. The
// other one exist for legacy reasons since some old hosts might still use them
// (EnergyXT being the only known host on Linux that uses the `main` entry
// point).

// TODO: At some point we could use a similar per-library bridging strategy as
//       we use for VST3 and CLAP. We can use ELF constructors and destructors
//       to hook into the loading and unloading of this library, and then assign
//       multiple VST2 plugin instances to the same host process. That would
//       make VST2 bridging a bit more efficient without having to set up plugin
//       groups.

// These plugin libraries can be used in one of two ways: they can either be
// loaded directly (the yabridge <4.0 way), or they can be loaded indirectly
// from `yabridge-chainloader-*.so` (the yabridge >=4.0 way). The advantage of
// chainloading this library from a tiny stub library is that yabridge can be
// updated without having to also replace all of the library copies and that it
// takes up less space on filesystems that don't support reflinking, but the
// catch is that we no longer have one unique plugin bridge library per plugin.
// This means that we cannot store the current bridge instance as a global in
// this library (because it would then be shared by multiple chainloaders), and
// that we cannot use `dladdr()` within this library to get the path to the
// current plugin, because thatq would return the path to this shared plugin
// library instead. To accommodate for this, we'll provide the usual plugin
// entry points, and we'll also provide simple methods for initializing the
// bridge so that the chainloading library can hold on to the bridge instance
// instead of this library.

void log_init_error(const std::exception& error, const fs::path& plugin_path) {
    Logger logger = Logger::create_exception_logger();

    logger.log("");
    logger.log("Error during initialization:");
    logger.log(error.what());
    logger.log("");

    // Also show a desktop notification since most people likely won't see the
    // above message
    send_notification(
        "Failed to initialize VST2 plugin",
        error.what() +
            "\nCheck the plugin's output in a terminal for more information"s,
        plugin_path);
}

/**
 * The main VST2 plugin entry point for when this plugin library is used
 * directly. We first set up a bridge that connects to a Wine process that hosts
 * the Windows VST2 plugin. We then create and return a VST plugin struct that
 * acts as a passthrough to the bridge.
 *
 * To keep this somewhat contained this is the only place where we're doing
 * manual memory management. Clean up is done when we receive the `effClose`
 * opcode from the VST2 host (i.e. opcode 1).`
 */
extern "C" YABRIDGE_EXPORT AEffect* VSTPluginMain(
    audioMasterCallback host_callback) {
    assert(host_callback);

    const fs::path plugin_path = get_this_file_location();
    try {
        // This is the only place where we have to use manual memory management.
        // The bridge's destructor is called when the `effClose` opcode is
        // received.
        Vst2PluginBridge* bridge =
            new Vst2PluginBridge(plugin_path, host_callback);

        return &bridge->plugin_;
    } catch (const std::exception& error) {
        log_init_error(error, plugin_path);

        return nullptr;
    }
}

// XXX: GCC doens't seem to have a clean way to let you define an arbitrary
//      function called 'main'. Even JUCE does it this way, so it should be
//      safe.
extern "C" YABRIDGE_EXPORT AEffect* deprecated_main(
    audioMasterCallback audioMaster) asm("main");
YABRIDGE_EXPORT AEffect* deprecated_main(audioMasterCallback audioMaster) {
    return VSTPluginMain(audioMaster);
}

/**
 * This function can be called from the chainloader to initialize a new plugin
 * bridge instance. Since VST2 only has a single plugin entry point and plugins
 * clean up after themselves in `effClose()`, this is the only function the
 * chainloader will call.
 */
extern "C" YABRIDGE_EXPORT AEffect* yabridge_plugin_init(
    audioMasterCallback host_callback,
    const char* plugin_path) {
    assert(host_callback);
    assert(plugin_path);

    try {
        Vst2PluginBridge* bridge =
            new Vst2PluginBridge(plugin_path, host_callback);

        return &bridge->plugin_;
    } catch (const std::exception& error) {
        log_init_error(error, plugin_path);

        return nullptr;
    }
}

/**
 * Returns the yabridge version in use. Can be queried by hosts through the
 * chainloader. Both functions have the same name and signature.
 */
extern "C" YABRIDGE_EXPORT const char* yabridge_version() {
    return yabridge_git_version;
}
