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
#include <cassert>
#include <mutex>

#include <clap/entry.h>
#include <dlfcn.h>

// Generated inside of the build directory
#include <config.h>

#include "../common/linking.h"
#include "../common/utils.h"
#include "utils.h"

// These chainloader libraries are tiny, mostly dependencyless libraries that
// `dlopen()` the actual `libyabridge-{vst2,vst3}.so` files and forward the
// entry point function calls from this library to those. Or technically, these
// libraries use dedicated entry point functions because multiple chainloader
// libraries may all dynamically link to the exact same plugin library, so we
// can't store any bridge information in a global there. This approach avoids
// wasting disk space on copies on file systems that don't support reflinking,
// but more importantly it also avoids the need to rerun `yabridgectl sync`
// whenever yabridge is updated. This is even more important when considering
// distro packaging, because updates to Boost might require the package to be
// rebuilt, which in turn would also require a resync.

namespace fs = ghc::filesystem;

// These functions are loaded from `libyabridge-clap.so` the first time
// `clap_entry.init` gets called
using ClapPluginBridge = void;

ClapPluginBridge* (*yabridge_module_init)(const char* plugin_path) = nullptr;
void (*yabridge_module_free)(ClapPluginBridge* instance) = nullptr;
const void* (*yabridge_module_get_factory)(ClapPluginBridge* instance,
                                           const char* factory_id) = nullptr;

// This bridges the `yabridge_version()` call from the plugin library. This
// function was added later, so through weird version mixing it may be missing
// on the yabridge library.
char* (*remote_yabridge_version)() = nullptr;

/**
 * The bridge instance for this chainloader. This is initialized when
 * `clap_entry.init` first gets called.
 */
std::unique_ptr<ClapPluginBridge, decltype(yabridge_module_free)> bridge(
    nullptr,
    nullptr);
/**
 * The number of active instances. Incremented when `clap_entry_init()` is
 * called, decremented when `clap_entry_exit()` is called. We'll initialize the
 * bridge when this is first incremented from 0, and we'll free the bridge again
 * when a `clap_entry_exit()` call causes this to return back to 0.
 */
std::atomic_size_t active_instances = 0;

/**
 * The first time one of the exported functions from this library gets called,
 * we'll need to load the corresponding `libyabridge-*.so` file and fetch the
 * the entry point functions from that file.
 */
bool initialize_library() {
    static void* library_handle = nullptr;
    static std::mutex library_handle_mutex;

    std::lock_guard lock(library_handle_mutex);

    // There should be no situation where this library gets loaded and then two
    // threads immediately start calling functions, but we'll handle that
    // situation just in case it does happen
    if (library_handle) {
        return true;
    }

    library_handle = find_plugin_library(yabridge_clap_plugin_name);
    if (!library_handle) {
        return false;
    }

#define MAYBE_LOAD_FUNCTION(name)                                           \
    do {                                                                    \
        (name) =                                                            \
            reinterpret_cast<decltype(name)>(dlsym(library_handle, #name)); \
    } while (false)
#define LOAD_FUNCTION(name)                                      \
    do {                                                         \
        MAYBE_LOAD_FUNCTION(name);                               \
        if (!(name)) {                                           \
            log_failing_dlsym(yabridge_clap_plugin_name, #name); \
            return false;                                        \
        }                                                        \
    } while (false)

    LOAD_FUNCTION(yabridge_module_init);
    LOAD_FUNCTION(yabridge_module_free);
    LOAD_FUNCTION(yabridge_module_get_factory);
    MAYBE_LOAD_FUNCTION(remote_yabridge_version);

#undef LOAD_FUNCTION
#undef MAYBE_LOAD_FUNCTION

    return true;
}

bool clap_entry_init(const char* /*plugin_path*/) {
    // This function can be called multiple times, so we should make sure to
    // only initialize the bridge on the first call
    if (active_instances.fetch_add(1, std::memory_order_seq_cst) == 0) {
        if (!initialize_library()) {
            return false;
        }

        // You can't change the deleter function with `.reset()` so we'll need
        // this abomination instead
        // XXX: The host also provides us with the plugin path which we could
        //      just use instead. Should we? The advantage of doing it this way
        //      instead is that we'll have consistent behavior between all
        //      plugin formats.
        const fs::path this_plugin_path = get_this_file_location();
        bridge =
            decltype(bridge)(yabridge_module_init(this_plugin_path.c_str()),
                             yabridge_module_free);
        if (!bridge) {
            return false;
        }
    }

    return true;
}

void clap_entry_deinit() {
    // We'll free the bridge when this exits brings the reference count back to
    // zero
    if (active_instances.fetch_sub(1, std::memory_order_seq_cst) == 1) {
        bridge.reset();
    }
}

const void* clap_entry_get_factory(const char* factory_id) {
    // The host should have called `clap_entry.init` first
    assert(bridge);
    assert(factory_id);

    return yabridge_module_get_factory(bridge.get(), factory_id);
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
 * This returns the actual yabridge library's version through
 * `yabridge_version()`. Reporting the version associated with this chainloader
 * wouldn't be very useful, and that would also cause the chainloader to be
 * rebuilt on every git commit in development.
 */
extern "C" YABRIDGE_EXPORT char* yabridge_version() {
    if (!initialize_library() || !remote_yabridge_version) {
        return nullptr;
    }

    return remote_yabridge_version();
}
