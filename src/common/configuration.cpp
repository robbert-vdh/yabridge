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

#include "configuration.h"

#include <fnmatch.h>
#include <fstream>

// By default tomlplusplus is no longer headers when using the package config
// file. We don't want to link against third party shared libraries in yabridge.
#ifdef TOML_SHARED_LIB

#undef TOML_SHARED_LIB
#undef TOML_HEADER_ONLY
#define TOML_HEADER_ONLY 1

#endif

// tomlplusplus recently got some Windows fixes, but they cause compilation
// errors and we don't need them so we'll just disable them outright. Disabling
// `TOML_ENABLE_WINDOWS_COMPAT` is no longer enough, and you can't disable
// `TOML_WINDOWS` directly. This is the same trick used in `use-asio-linux.h`.
#pragma push_macro("WIN32")
#pragma push_macro("_WIN32")
#pragma push_macro("__WIN32__")
#pragma push_macro("__NT__")
#pragma push_macro("__CYGWIN__")
#undef WIN32
#undef _WIN32
#undef __WIN32__
#undef __NT__
#undef __CYGWIN__

#include <toml++/toml.h>

#pragma pop_macro("WIN32")
#pragma pop_macro("_WIN32")
#pragma pop_macro("__WIN32__")
#pragma pop_macro("__NT__")
#pragma pop_macro("__CYGWIN__")

#include "utils.h"

namespace fs = ghc::filesystem;

Configuration::Configuration() noexcept {}

Configuration::Configuration(const fs::path& config_path,
                             const fs::path& yabridge_path)
    : Configuration() {
    // Will throw a `toml::parsing_error` if the file cannot be parsed. Better
    // to throw here rather than failing silently since syntax errors would
    // otherwise be impossible to spot. We'll also have to sort all tables by
    // the location in the file since tomlplusplus internally uses ordered maps
    // so otherwise we'll get the tables sorted by key instead.
    toml::table table = toml::parse_file(config_path.string());

    // This table stores its children in an ordered map and it will thus be
    // sorted lexicographically. For our uses we want sections from the start of
    // the file to have precedence over later sections, so we need to sort the
    // tables by source location first.
    std::vector<std::tuple<toml::key, toml::table>> sorted_tables{};
    for (auto [pattern, node] : table) {
        if (const toml::table* config = node.as_table()) {
            sorted_tables.push_back(std::make_tuple(pattern, *config));
        }
    }
    std::sort(sorted_tables.begin(), sorted_tables.end(),
              [](const auto& a, const auto& b) {
                  const auto& [a_pattern, a_table] = a;
                  const auto& [b_pattern, b_table] = b;

                  return a_pattern.source().begin.line <
                         b_pattern.source().begin.line;
              });

    // This is the path of the current .so file relative to this `yabridge.toml`
    // file
    const fs::path relative_path =
        yabridge_path.lexically_relative(config_path.parent_path());
    for (const auto& [pattern, table] : sorted_tables) {
        // First try to match the glob pattern, allow matching an entire
        // directory for ease of use. If none of the patterns in the file match
        // the plugin path then everything will be left at the defaults.
        const std::string key(pattern.str());
        if (fnmatch(key.c_str(), relative_path.c_str(),
                    FNM_PATHNAME | FNM_LEADING_DIR) != 0) {
            continue;
        }

        matched_file = config_path;
        matched_pattern = pattern;

        // If the table is missing some fields then they will simply be left at
        // their defaults. At this point I'd really wish C++ could do pattern
        // matching.
        for (const auto& [key, value] : table) {
            if (key == "group") {
                if (const auto parsed_value = value.as_string()) {
                    group = parsed_value->get();
                } else {
                    invalid_options.emplace_back(key);
                }
            } else if (key == "disable_pipes") {
                // This option can be either enabled or disable with a boolean,
                // or it can be set to an absolute path
                if (const auto parsed_value = value.as_boolean()) {
                    if (*parsed_value) {
                        disable_pipes = get_temporary_directory() /
                                        "yabridge-plugin-output.log";
                    } else {
                        disable_pipes = std::nullopt;
                    }
                } else if (const auto parsed_value = value.as_string()) {
                    disable_pipes = parsed_value->get();
                } else {
                    invalid_options.emplace_back(key);
                }
            } else if (key == "editor_coordinate_hack") {
                if (const auto parsed_value = value.as_boolean()) {
                    editor_coordinate_hack = parsed_value->get();
                } else {
                    invalid_options.emplace_back(key);
                }
            } else if (key == "editor_disable_host_scaling") {
                if (const auto parsed_value = value.as_boolean()) {
                    editor_disable_host_scaling = parsed_value->get();
                } else {
                    invalid_options.emplace_back(key);
                }
            } else if (key == "editor_force_dnd") {
                if (const auto parsed_value = value.as_boolean()) {
                    editor_force_dnd = parsed_value->get();
                } else {
                    invalid_options.emplace_back(key);
                }
            } else if (key == "editor_xembed") {
                if (const auto parsed_value = value.as_boolean()) {
                    editor_xembed = parsed_value->get();
                } else {
                    invalid_options.emplace_back(key);
                }
            } else if (key == "frame_rate") {
                if (const auto parsed_value = value.as_floating_point()) {
                    frame_rate = parsed_value->get();
                } else if (const auto parsed_value = value.as_integer()) {
                    // For usability's sake we want to be a bit more lax than a
                    // normal TOML file would be and accept both floating point
                    // values and integers here
                    frame_rate = parsed_value->get();
                } else {
                    invalid_options.emplace_back(key);
                }
            } else if (key == "hide_daw") {
                if (const auto parsed_value = value.as_boolean()) {
                    hide_daw = parsed_value->get();
                } else {
                    invalid_options.emplace_back(key);
                }
            } else if (key == "vst3_prefer_32bit") {
                if (const auto parsed_value = value.as_boolean()) {
                    vst3_prefer_32bit = parsed_value->get();
                } else {
                    invalid_options.emplace_back(key);
                }
            } else {
                unknown_options.emplace_back(key);
            }
        }

        break;
    }
}

std::chrono::steady_clock::duration Configuration::event_loop_interval()
    const noexcept {
    return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::milliseconds(1000) / frame_rate.value_or(60.0));
}
