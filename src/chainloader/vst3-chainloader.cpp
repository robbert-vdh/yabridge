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

#include <atomic>
#include <cassert>
#include <mutex>

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

using Vst3PluginBridge = void;
using PluginFactory = void;

// These functions are loaded from `libyabridge-vst3.so` the first time
// `ModuleEntry()` gets called
Vst3PluginBridge* (*yabridge_module_init)(const char* plugin_path) = nullptr;
void (*yabridge_module_free)(Vst3PluginBridge* instance) = nullptr;
PluginFactory* (*yabridge_module_get_plugin_factory)(
    Vst3PluginBridge* instance) = nullptr;

/**
 * The bridge instance for this chainloader. This is initialized when
 * `ModuleEntry()` first gets called.
 */
std::unique_ptr<Vst3PluginBridge, decltype(yabridge_module_free)> bridge(
    nullptr,
    nullptr);
/**
 * The number of active instances. Incremented when `ModuleEntry()` is called,
 * decremented when `ModuleExit()` is called. We'll initialize the bridge when
 * this is first incremented from 0, and we'll free the bridge again when a
 * `ModuleExit()` call causes this to return back to 0.
 */
std::atomic_size_t active_instances = 0;
std::mutex bridge_mutex;

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

    library_handle = find_plugin_library(yabridge_vst3_plugin_name);
    if (!library_handle) {
        return false;
    }

#define LOAD_FUNCTION(name)                                                 \
    do {                                                                    \
        (name) =                                                            \
            reinterpret_cast<decltype(name)>(dlsym(library_handle, #name)); \
        if (!(name)) {                                                      \
            log_failing_dlsym(yabridge_vst3_plugin_name, #name);            \
            return false;                                                   \
        }                                                                   \
    } while (false)

    LOAD_FUNCTION(yabridge_module_init);
    LOAD_FUNCTION(yabridge_module_free);
    LOAD_FUNCTION(yabridge_module_get_plugin_factory);

#undef LOAD_FUNCTION

    return true;
}

extern "C" YABRIDGE_EXPORT bool ModuleEntry(void*) {
    // This function can be called multiple times, so we should make sure to
    // only initialize the bridge on the first call
    if (active_instances.fetch_add(1, std::memory_order_seq_cst) == 0) {
        if (!initialize_library()) {
            return false;
        }

        // You can't change the deleter function with `.reset()` so we'll need
        // this abomination instead
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

extern "C" YABRIDGE_EXPORT bool ModuleExit() {
    // We'll free the bridge when this exits brings the reference count back to
    // zero
    if (active_instances.fetch_sub(1, std::memory_order_seq_cst) == 1) {
        bridge.reset();
    }

    return true;
}

extern "C" YABRIDGE_EXPORT PluginFactory* GetPluginFactory() {
    // The host should have called `InitModule()` first
    assert(bridge);

    return yabridge_module_get_plugin_factory(bridge.get());
}
