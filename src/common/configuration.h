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

#pragma once

#ifdef __WINE__
#include "../wine-host/boost-fix.h"
#endif
#include <boost/filesystem.hpp>

#include <bitsery/ext/std_optional.h>
#include <optional>

#include "bitsery/ext/boost-path.h"

/**
 * An object that's used to provide plugin-specific configuration. Right now
 * this is only used to declare plugin groups. A plugin group is a set of
 * plugins that will be hosted in the same process rather than individually so
 * they can share resources. Configuration file loading works as follows:
 *
 * 1. `load_config_for(path)` from `src/plugin/utils.h` gets called with a path
 *    to the copy of or symlink to `libyabridge.so` that the plugin host has
 *    tried to load.
 * 2. We start looking for a file named `yabridge.toml` in the same directory as
 *    that `.so` file, iteratively continuing to search one directory higher
 *    until we either find the file or we reach the filesystem root.
 * 3. If the file is found, then parse it as a TOML file and look for the first
 *    table whose key is a glob pattern that (partially) matches the relative
 *    path between the found `yabridge.toml` and the `.so` file. As a rule of
 *    thumb, if the `find <pattern> -type f` command executed in Bash would list
 *    the `.so` file, then the following table in `yabridge.tmol` would also
 *    match the same `.so` file:
 *
 *    ```toml
 *    ["<patern>"]
 *    group = "..."
 *    ```
 * 4. If one of these glob patterns could be matched with the relative path of
 *    the `.so` file then we'll use the settings specified in that section.
 *    Otherwise the default settings will be used.
 */
class Configuration {
   public:
    /**
     * Create an empty configuration object with default settings.
     */
    Configuration();

    /**
     * Load the configuration for an instance of yabridge from a configuration
     * file by matching the plugin's relative path to the glob patterns in that
     * configuration file. Will leave the object empty if the plugin cannot be
     * matched to any of the patterns. Not meant to be used directly.
     *
     * @throw toml::parsing_error If the file could not be parsed.
     *
     * @see ../plugin/utils.h:load_config_for
     */
    Configuration(const boost::filesystem::path& config_path,
                  const boost::filesystem::path& yabridge_path);

    /**
     * If this is set to true, then the plugin editor should be embedded in yet
     * another window. This would result in an embedding sequence of
     * `<window_provided_by_host> <-> <wine_parent_window> <->
     * <wine_child_window> <-> <window_created_by_plugin>`, where
     * `<wine_child_window>` is the new addition. The only plugin I've
     * encountered where this was necessary was PSPaudioware E27 (and it likely
     * also applies to other PSPaudioware plugins with expandable GUIs). I also
     * haven't noticed any issues caused from having this enabled, but having it
     * behind a flag reduces the amount of moving parts so that's probably a
     * better idea.
     */
    bool editor_double_embed = false;

    /**
     * If this is set to true, then any calls to `audioMasterUpdateDisplay()`
     * will automatically return 0 without being sent to the host. This is a
     * HACK to work around implementations issues in REAPER and Renoise, see #29
     * and #32.
     */
    bool hack_reaper_update_display = false;

    /**
     * The name of the plugin group that should be used for the plugin this
     * configuration object was created for. If not set, then the plugin should
     * be hosted individually instead.
     */
    std::optional<std::string> group;

    /**
     * The path to the configuration file that was parsed.
     */
    std::optional<boost::filesystem::path> matched_file;

    /**
     * The matched glob pattern in the above configuration file.
     */
    std::optional<std::string> matched_pattern;

    /**
     * Options with a wrong argument type. These will be printed separately from
     * `unknown_options` to avoid confusion.
     */
    std::vector<std::string> invalid_options;

    /**
     * Unrecognized configuration options, likely caused by an old option that
     * served as a hack or a workaround getting removed. Will be printed on
     * startup when not empty.
     */
    std::vector<std::string> unknown_options;

    template <typename S>
    void serialize(S& s) {
        s.value1b(editor_double_embed);
        s.value1b(hack_reaper_update_display);
        s.ext(group, bitsery::ext::StdOptional(),
              [](S& s, auto& v) { s.text1b(v, 4096); });
        s.ext(matched_file, bitsery::ext::StdOptional(),
              [](S& s, auto& v) { s.ext(v, bitsery::ext::BoostPath()); });
        s.ext(matched_pattern, bitsery::ext::StdOptional(),
              [](S& s, auto& v) { s.text1b(v, 4096); });

        s.container(invalid_options, 1024,
                    [](S& s, auto& v) { s.text1b(v, 4096); });
        s.container(unknown_options, 1024,
                    [](S& s, auto& v) { s.text1b(v, 4096); });
    }
};
