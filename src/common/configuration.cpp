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

#include "configuration.h"

// tomlplusplus recently got some Windows fixes, but they cause compilation
// errors and we don't need them so we'll just disable them outright
#define TOML_WINDOWS_COMPAT 0

#include <fnmatch.h>
#include <toml++/toml.h>
#include <fstream>

namespace fs = boost::filesystem;

Configuration::Configuration() {}

Configuration::Configuration(const fs::path& config_path,
                             const fs::path& yabridge_path)
    : Configuration() {
    // Will throw a `toml::parsing_error` if the file cannot be parsed. Better
    // to throw here rather than failing silently since syntax errors would
    // otherwise be impossible to spot. We'll also have to sort all tables by
    // the location in the file since tomlplusplus internally uses ordered maps
    // so otherwise we'll get the tables sorted by key instead.
    toml::table table = toml::parse_file(config_path.string());

    // I wasn't able to wade through the template soup to come up with a better
    // way to sort this by location, so please feel free to correct this if you
    // know of a better way! The source locations has to be stored inside of the
    // vector itself because the `node.source()` on the copies stored in this
    // vector won't contain the proper location after we've iterated through
    // `table`.
    std::vector<std::tuple<std::string, toml::source_region, toml::table>>
        sorted_tables{};
    for (auto [pattern, node] : table) {
        if (const toml::table* config = node.as_table()) {
            sorted_tables.push_back(
                std::make_tuple(pattern, config->source(), *config));
        }
    }
    std::sort(sorted_tables.begin(), sorted_tables.end(),
              [](const auto& a, const auto& b) {
                  const auto& [a_pattern, a_source, a_table] = a;
                  const auto& [b_pattern, b_source, b_table] = b;

                  return a_source.begin.line < b_source.begin.line;
              });

    const fs::path relative_path =
        yabridge_path.lexically_relative(config_path.parent_path());
    for (const auto& [pattern, source, table] : sorted_tables) {
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
        // their defaults. At this point I'd really wish C++ could do pattern
        // matching.
        for (const auto& [key, value] : table) {
            if (key == "cache_time_info") {
                if (const auto parsed_value = value.as_boolean()) {
                    cache_time_info = parsed_value->get();
                } else {
                    invalid_options.push_back(key);
                }
            } else if (key == "editor_double_embed") {
                if (const auto parsed_value = value.as_boolean()) {
                    editor_double_embed = parsed_value->get();
                } else {
                    invalid_options.push_back(key);
                }
            } else if (key == "editor_xembed") {
                if (const auto parsed_value = value.as_boolean()) {
                    editor_xembed = parsed_value->get();
                } else {
                    invalid_options.push_back(key);
                }
            } else if (key == "group") {
                if (const auto parsed_value = value.as_string()) {
                    group = parsed_value->get();
                } else {
                    invalid_options.push_back(key);
                }
            } else {
                unknown_options.push_back(key);
            }
        }

        break;
    }
}
