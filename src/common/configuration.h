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

#pragma once

#include <chrono>
#include <optional>

#include <ghc/filesystem.hpp>

#include "bitsery/ext/ghc-path.h"
#include "bitsery/ext/in-place-optional.h"

/**
 * An object that's used to provide plugin-specific configuration. Right now
 * this is only used to declare plugin groups. A plugin group is a set of
 * plugins that will be hosted in the same process rather than individually so
 * they can share resources. Configuration file loading works as follows:
 *
 * 1. `load_config_for(path)` from `src/plugin/utils.h` gets called with a path
 *    to the copy of or symlink to `libyabridge-{vst2,vst3}.so` that the plugin
 *    host has tried to load.
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
    Configuration() noexcept;

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
    Configuration(const ghc::filesystem::path& config_path,
                  const ghc::filesystem::path& yabridge_path);

    /**
     * The name of the plugin group that should be used for the plugin this
     * configuration object was created for. If not set, then the plugin should
     * be hosted individually instead.
     */
    std::optional<std::string> group;

    /**
     * If enabled, we'll redirect the plugin's STDOUT and STDERR streams to this
     * file instead of using pipes to intersperse it with yabridge's other
     * output. This is necessary for _ujam_ plugins to work since they for some
     * reason will throw `JS_EXEC_FAILED` errors when either STDOUT or STDERR is
     * a pipe.
     *
     * This option can be set to a boolean, in which case we'll set the path to
     * `<temporary_directory>/yabridge-plugin-output.log`, or it can be set to
     * an absolute path. (we don't try to expand tildes)
     */
    std::optional<ghc::filesystem::path> disable_pipes;

    /**
     * If this is set to `true`, then the after every resize we will move the
     * embedded Wine window back to `(0, 0)` and then do the coordinate fixing
     * trick again. This may be useful with buggy plugins that draw their GUI
     * based on the (top level) window's position. Otherwise those GUIs will be
     * offset by the window's actual position on screen. The only plugins I've
     * encountered where this was necessary were PSPaudioware E27 and Soundtoys
     * Crystallizer. This is not enabled by default, because it also interferes
     * with resize handles.
     */
    bool editor_coordinate_hack = false;

    /**
     * If set to `true`, we'll remove the `XdndAware` property all ancestor
     * windows in `editor.cpp`. This is needed for REAPER as REAPER implements
     * (but doesn't use) drag-and-drop support on all of its windows. This
     * causes the FX window to intercept the drop thus making it impossible to
     * drag files onto plugin editors, native or otherwise.
     */
    bool editor_force_dnd = false;

    /**
     * Use XEmbed instead of yabridge's normal editor embedding method. Wine's
     * XEmbed support is not very polished yet and tends to lead to rendering
     * issues, so this is disabled by default. Also, editor resizing won't work
     * reliably when XEmbed is enabled.
     */
    bool editor_xembed = false;

    /**
     * The number of times per second we'll handle the event loop. In most
     * plugins this also controls the plugin editor GUI's refresh rate.
     *
     * This defaults to 60 fps, but we'll store it in an optional as we only
     * want to show it in the startup message if this setting has explicitly
     * been set.
     *
     * @relates event_loop_interval
     */
    std::optional<float> frame_rate;

    /**
     * When this option is enabled, we'll report some random other string
     * instead of the actual name of the host when the plugin queries it. This
     * can sometimes be useful when a plugin has undesirable host-specific
     * behaviour. See the readme for some examples of where this might be
     * useful.
     */
    bool hide_daw = false;

    /**
     * Disable `IPlugViewContentScaleSupport::setContentScaleFactor()`. Wine
     * does not properly implement fractional DPI scaling, so without this
     * option plugins using GDI+ would draw their editor GUIs at the normal size
     * even though their window would actually be scaled. That would result in
     * giant black borders at the top and the right of the window. It appears
     * that with a Wine font DPI of 192 plugins often do draw correctly at 200%
     * scale.
     */
    bool vst3_no_scaling = false;

    /**
     * If a merged bundle contains both the 64-bit and the 32-bit versions of a
     * Windows VST3 plugin (in the `x86_64-win` and the `x86-win` directories),
     * then yabridge will use the 64-bit version by default. This option
     * overrides that preference and thus allows you to use the 32-bit version
     * if that's for whatever reason necessary.
     */
    bool vst3_prefer_32bit = false;

    /**
     * The path to the configuration file that was parsed.
     */
    std::optional<ghc::filesystem::path> matched_file;

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

    /**
     * The delay in milliseconds between calls to the event loop and to
     * `effEditIdle` for VST2 plugins. This is based on `frame_rate`.
     */
    std::chrono::steady_clock::duration event_loop_interval() const noexcept;

    template <typename S>
    void serialize(S& s) {
        s.ext(group, bitsery::ext::InPlaceOptional(),
              [](S& s, auto& v) { s.text1b(v, 4096); });

        s.ext(disable_pipes, bitsery::ext::InPlaceOptional(),
              [](S& s, auto& v) { s.ext(v, bitsery::ext::GhcPath{}); });
        s.value1b(editor_coordinate_hack);
        s.value1b(editor_force_dnd);
        s.value1b(editor_xembed);
        s.ext(frame_rate, bitsery::ext::InPlaceOptional(),
              [](S& s, auto& v) { s.value4b(v); });
        s.value1b(hide_daw);
        s.value1b(vst3_no_scaling);
        s.value1b(vst3_prefer_32bit);

        s.ext(matched_file, bitsery::ext::InPlaceOptional(),
              [](S& s, auto& v) { s.ext(v, bitsery::ext::GhcPath{}); });
        s.ext(matched_pattern, bitsery::ext::InPlaceOptional(),
              [](S& s, auto& v) { s.text1b(v, 4096); });

        s.container(invalid_options, 1024,
                    [](S& s, auto& v) { s.text1b(v, 4096); });
        s.container(unknown_options, 1024,
                    [](S& s, auto& v) { s.text1b(v, 4096); });
    }
};
