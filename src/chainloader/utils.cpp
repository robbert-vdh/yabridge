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

#include "utils.h"

#include <dlfcn.h>

// Generated inside of the build directory
#include <config.h>

#include "../common/linking.h"
#include "../common/logging/common.h"
#include "../common/notifications.h"
#include "../common/process.h"

namespace fs = ghc::filesystem;

void* find_plugin_library(const std::string& name) {
    // Just using a goto for this would probably be cleaner, but yeah...
    const auto impl = [&name]() -> void* {
        // If `name` exists right next to the Wine plugin host binary, then
        // we'll try loading that. Otherwise we'll fall back to regular
        // `dlopen()` for distro packaged versions of yabridge
        const std::vector<fs::path> search_path = get_augmented_search_path();
        if (const auto& yabridge_host_path =
                search_in_path(search_path, yabridge_host_name)) {
            const fs::path candidate = yabridge_host_path->parent_path() / name;
            if (fs::exists(candidate)) {
                return dlopen(candidate.c_str(), RTLD_LAZY | RTLD_LOCAL);
            }
        }

        if (const auto& yabridge_host_32_path =
                search_in_path(search_path, yabridge_host_name_32bit)) {
            const fs::path candidate =
                yabridge_host_32_path->parent_path() / name;
            if (fs::exists(candidate)) {
                return dlopen(candidate.c_str(), RTLD_LAZY | RTLD_LOCAL);
            }
        }

        if (void* handle = dlopen(name.c_str(), RTLD_LAZY | RTLD_LOCAL)) {
            return handle;
        }

        // One last Hail Mary, in case ldconfig was not set up correctly. This
        // might be relevant for some of the `/usr/local/*` locations (although
        // you really, really shouldn't install yabridge there, please, thank
        // you). Yabridgectl searches through these same directories.
        for (const auto& lib_dir : {
                 "/usr/lib",
                 "/usr/lib/x86_64-linux-gnu",
                 "/usr/lib64",
                 "/usr/local/lib",
                 "/usr/local/lib/x86_64-linux-gnu",
                 "/usr/local/lib64",
             }) {
            const fs::path candidate = fs::path(lib_dir) / name;
            if (void* handle =
                    dlopen(candidate.c_str(), RTLD_LAZY | RTLD_LOCAL)) {
                return handle;
            }
        }

        return nullptr;
    };

    void* handle = impl();
    if (!handle) {
        const fs::path this_plugin_path = get_this_file_location();

        Logger logger = Logger::create_exception_logger();

        logger.log("");
        logger.log("Could not find '" + name + "'");
        logger.log("");
        logger.log(
            "Make sure you followed the installation instructions from "
            "yabridge's readme.");
        logger.log("");
        logger.log("Source: '" + this_plugin_path.string() + "'");
        logger.log("");

        // Also show a desktop notification since most people likely won't see
        // the above message
        send_notification("Could not find '" + name + "'",
                          "Make sure you followed the installation "
                          "instructions from yabridge's readme",
                          this_plugin_path);
    }

    return handle;
}
