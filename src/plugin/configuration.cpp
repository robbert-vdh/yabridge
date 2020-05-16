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

#include "configuration.h"

#include <fnmatch.h>
#include <toml++/toml.h>
#include <fstream>

#include "utils.h"

namespace fs = boost::filesystem;

Configuration::Configuration() {}

Configuration::Configuration(const fs::path& config_path,
                             const fs::path& yabridge_path)
    : Configuration() {
    // Will throw a `toml::parsing_error` if the file cannot be parsed. Better
    // to throw here rather than failing silently since syntax errors would
    // otherwise be impossible to spot.
    toml::table table = toml::parse_file(config_path.string());

    const fs::path relative_path =
        yabridge_path.lexically_relative(config_path.parent_path());
    for (const auto& [pattern, value] : table) {
        // First try to match the glob pattern, allow matching an entire
        // directory for ease of use. If none of the patterns in the file match
        // the plugin path then everything will be left at the defaults.
        if (fnmatch(pattern.c_str(), relative_path.c_str(),
                    FNM_PATHNAME | FNM_LEADING_DIR) != 0) {
            continue;
        }

        matched_file = config_path;
        matched_pattern = pattern;

        // If the table is missing some fields then they will simply be left at
        // their defaults
        if (toml::table* config = value.as_table(); config != nullptr) {
            group = (*config)["group"].value<std::string>();
        }

        break;
    }
}

Configuration Configuration::load_for(const fs::path& yabridge_path) {
    // First find the closest `yabridge.tmol` file for the plugin, falling back
    // to default configuration settings if it doesn't exist
    const std::optional<fs::path> config_file =
        find_dominating_file("yabridge.toml", yabridge_path);
    if (!config_file.has_value()) {
        return Configuration();
    }

    // TODO: Disable plugin groups when not compiling with plugin group support
    return Configuration(config_file.value(), yabridge_path);
}
