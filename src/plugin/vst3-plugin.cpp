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

#include "../common/linking.h"
#include "bridges/vst3.h"

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

/**
 * The global plugin bridge instance. Only used if this plugin library is used
 * directly. When the library is chainloaded, this will remain a null pointer.
 */
std::unique_ptr<Vst3PluginBridge> bridge;

void log_init_exception(const std::exception& error,
                        const fs::path& plugin_path) {
    Logger logger = Logger::create_exception_logger();

    logger.log("");
    logger.log("Error during initialization:");
    logger.log(error.what());
    logger.log("");

    // Also show a desktop notification since most people likely won't see the
    // above message
    send_notification(
        "Failed to initialize VST3 plugin",
        error.what() +
            "\nCheck the plugin's output in a terminal for more information"s,
        plugin_path);
}

// These functions are called by the `ModuleEntry` and `ModuleExit` functions on
// the first load and load unload. The chainloader library has similar functions
// that call the `yabridge_module_` functions exported at the bottom of
// this file.
bool InitModule() {
    assert(!bridge);

    const fs::path plugin_path = get_this_file_location();
    try {
        bridge = std::make_unique<Vst3PluginBridge>(plugin_path);

        return true;
    } catch (const std::exception& error) {
        log_init_exception(error, plugin_path);

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
extern "C" YABRIDGE_EXPORT Steinberg::IPluginFactory* PLUGIN_API
GetPluginFactory() {
    // The host should have called `InitModule()` first
    assert(bridge);

    return bridge->get_plugin_factory();
}

/**
 * This function can be called from the chainloader to initialize a new plugin
 * bridge instance. The caller should store the pointer and later free it again
 * using the `yabridge_module_free()` function. If the bridge could not
 * initialize due to an error, then the error will be logged and a null pointer
 * will be returned.
 */
extern "C" YABRIDGE_EXPORT Vst3PluginBridge* yabridge_module_init(
    const char* plugin_path) {
    assert(plugin_path);

    try {
        return new Vst3PluginBridge(plugin_path);
    } catch (const std::exception& error) {
        log_init_exception(error, plugin_path);

        return nullptr;
    }
}

/**
 * Free a bridge instance returned by `yabridge_module_init`.
 */
extern "C" YABRIDGE_EXPORT void yabridge_module_free(
    Vst3PluginBridge* instance) {
    if (instance) {
        delete instance;
    }
}

/**
 * Our VST3 plugin's entry point. When building the plugin factory we'll host
 * the plugin in our Wine application, retrieve its information and supported
 * classes, and then recreate it here.
 */
extern "C" YABRIDGE_EXPORT Steinberg::IPluginFactory*
yabridge_module_get_plugin_factory(Vst3PluginBridge* instance) {
    assert(instance);

    return instance->get_plugin_factory();
}
