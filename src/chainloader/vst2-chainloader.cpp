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
// `dlopen()` the actual `libyabridge-{clap,vst2,vst3}.so` files and forward the
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

using audioMasterCallback = void*;
using AEffect = void;

// These functions are loaded from `libyabridge-vst3.so` the first time
// `VSTPluginMain` gets called
AEffect* (*yabridge_plugin_init)(audioMasterCallback host_callback,
                                 const char* plugin_path) = nullptr;

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

    library_handle = find_plugin_library(yabridge_vst2_plugin_name);
    if (!library_handle) {
        return false;
    }

#define LOAD_FUNCTION(name)                                                 \
    do {                                                                    \
        (name) =                                                            \
            reinterpret_cast<decltype(name)>(dlsym(library_handle, #name)); \
        if (!(name)) {                                                      \
            log_failing_dlsym(yabridge_vst2_plugin_name, #name);            \
            return false;                                                   \
        }                                                                   \
    } while (false)

    LOAD_FUNCTION(yabridge_plugin_init);

#undef LOAD_FUNCTION

    return true;
}

extern "C" YABRIDGE_EXPORT AEffect* VSTPluginMain(
    audioMasterCallback host_callback) {
    assert(host_callback);

    if (!initialize_library()) {
        return nullptr;
    }

    const fs::path this_plugin_path = get_this_file_location();
    return yabridge_plugin_init(host_callback, this_plugin_path.c_str());
}

// XXX: GCC doens't seem to have a clean way to let you define an arbitrary
//      function called 'main'. Even JUCE does it this way, so it should be
//      safe.
extern "C" YABRIDGE_EXPORT AEffect* deprecated_main(
    audioMasterCallback audioMaster) asm("main");
YABRIDGE_EXPORT AEffect* deprecated_main(audioMasterCallback audioMaster) {
    return VSTPluginMain(audioMaster);
}
