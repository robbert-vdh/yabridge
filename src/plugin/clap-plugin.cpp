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

#include <atomic>

#include <clap/entry.h>

// Generated inside of the build directory
#include <version.h>

#include "bridges/clap.h"

using namespace std::literals::string_literals;

namespace fs = ghc::filesystem;

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
 * The number of active instances. Incremented when `clap_entry_init()` is
 * called, decremented when `clap_entry_exit()` is called. We'll initialize the
 * bridge when this is first incremented from 0, and we'll free the bridge again
 * when a `clap_entry_exit()` call causes this to return back to 0.
 */
std::atomic_size_t active_instances = 0;
/**
 * The global plugin bridge instance. Only used if this plugin library is used
 * directly. When the library is chainloaded, this will remain a null pointer.
 */
std::unique_ptr<ClapPluginBridge> bridge;

void log_init_error(const std::exception& error, const fs::path& plugin_path) {
    Logger logger = Logger::create_exception_logger();

    logger.log("");
    logger.log("Error during initialization:");
    logger.log(error.what());
    logger.log("");

    // Also show a desktop notification since most people likely won't see the
    // above message
    send_notification(
        "Failed to initialize CLAP plugin",
        error.what() +
            "\nCheck the plugin's output in a terminal for more information"s,
        plugin_path);
}

bool clap_entry_init(const char* /*plugin_path*/) {
    // This function can be called multiple times, so we should make sure to
    // only initialize the bridge on the first call
    if (active_instances.fetch_add(1, std::memory_order_seq_cst) == 0) {
        assert(!bridge);

        // XXX: The host also provides us with the plugin path which we could
        //      just use instead. Should we? The advantage of doing it this way
        //      instead is that we'll have consistent behavior between all
        //      plugin formats.
        const fs::path plugin_path = get_this_file_location();
        try {
            bridge = std::make_unique<ClapPluginBridge>(plugin_path);

            return true;
        } catch (const std::exception& error) {
            log_init_error(error, plugin_path);

            return false;
        }
    }

    return true;
}

void clap_entry_deinit() {
    // We'll free the bridge when this exits brings the reference count back to
    // zero
    if (active_instances.fetch_sub(1, std::memory_order_seq_cst) == 1) {
        assert(bridge);

        bridge.reset();
    }
}

const void* clap_entry_get_factory(const char* factory_id) {
    assert(bridge);
    assert(factory_id);

    return bridge->get_factory(factory_id);
}

// This visibility attribute doesn't do anything on data with external linkage,
// but we'll include it here just because it's in the CLAP template
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init = clap_entry_init,
    .deinit = clap_entry_deinit,
    .get_factory = clap_entry_get_factory,
};

#pragma GCC diagnostic pop

/**
 * This function can be called from the chainloader to initialize a new plugin
 * bridge instance. The caller should store the pointer and later free it again
 * using the `yabridge_module_free()` function. If the bridge could not
 * initialize due to an error, then the error will be logged and a null pointer
 * will be returned.
 */
extern "C" YABRIDGE_EXPORT ClapPluginBridge* yabridge_module_init(
    const char* plugin_path) {
    assert(plugin_path);

    try {
        return new ClapPluginBridge(plugin_path);
    } catch (const std::exception& error) {
        log_init_error(error, plugin_path);

        return nullptr;
    }
}

/**
 * Free a bridge instance returned by `yabridge_module_init`.
 */
extern "C" YABRIDGE_EXPORT void yabridge_module_free(
    ClapPluginBridge* instance) {
    if (instance) {
        delete instance;
    }
}

/**
 * Create and return a factory from a bridge instance. Used by the chainloaders.
 */
extern "C" YABRIDGE_EXPORT const void* yabridge_module_get_factory(
    ClapPluginBridge* instance,
    const char* factory_id) {
    assert(instance);
    assert(factory_id);

    return instance->get_factory(factory_id);
}

/**
 * Returns the yabridge version in use. Can be queried by hosts through the
 * chainloader. Both functions have the same name and signature.
 */
extern "C" YABRIDGE_EXPORT const char* yabridge_version() {
    return yabridge_git_version;
}
