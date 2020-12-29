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

#include <variant>

#include <boost/process/async_pipe.hpp>
#include <boost/process/environment.hpp>

#include "../common/configuration.h"
#include "../common/plugins.h"

/**
 * Boost 1.72 was released with a known breaking bug caused by a missing
 * typedef: https://github.com/boostorg/process/issues/116.
 *
 * Luckily this is easy to fix since it's not really possible to downgrade Boost
 * as it would break other applications.
 *
 * Check if this is still needed for other distros after Arch starts packaging
 * Boost 1.73.
 */
class patched_async_pipe : public boost::process::async_pipe {
   public:
    using boost::process::async_pipe::async_pipe;

    typedef typename handle_type::executor_type executor_type;
};

/**
 * Marker struct for when we use the default Wine prefix.
 */
struct DefaultWinePrefix {};

/**
 * Marker struct for when the Wine prefix is overriden using the `WINEPREFIX`
 * environment variable.
 */
struct OverridenWinePrefix {
    boost::filesystem::path value;
};

/**
 * This will locate the plugin we're going to host based on the location of the
 * `.so` that we're currently operating from and provides information and
 * utility functions based on that.
 */
struct PluginInfo {
   public:
    /**
     * Locate the Windows plugin based on the location of this copy of
     * `libyabridge-{vst2,vst3}.so` file and the type of the plugin we're going
     * to load. For VST2 plugins this is a file with the same name but with a
     * `.dll` file extension instead of `.so`. In case this file does not exist
     * and the `.so` file is a symlink, we'll also repeat this check for the
     * file it links to. This is to support the workflow described in issue #3
     * where you use symlinks to copies of `libyabridge-vst2.so`.
     *
     * For VST3 plugins there is a strict format as defined by Steinberg, and
     * we'll have yabridgectl create a 'merged bundle' that also contains the
     * Windows VST3 plugin.
     *
     * TODO: At the moment we can't choose to use the 32-bit VST3 if a 64-bit
     *       plugin exists. Potential solutions are to add a config option to
     *       use the 32-bit version, or we can add a filename suffix to all
     *       32-bit versions so they can live alongside each other.
     *
     * @param plugin_type The type of the plugin we're going to load. The
     *   detection works slightly differently depending on the plugin type.
     *
     * @throw std::runtime_error If we cannot find a corresponding Windows
     *   plugin. The error message contains a human readable description of what
     *   went wrong.
     */
    PluginInfo(PluginType plugin_type);

    /**
     * Create the environment for the plugin host based on `wine_prefix`. If
     * `WINEPREFIX` was already set then nothing will be changed. Otherwise
     * we'll set `WINEPREFIX` to the detected Wine prefix, or it will be left
     * unset if we could not detect a prefix.
     */
    boost::process::environment create_host_env() const;

    /**
     * Return the path to the actual Wine prefix in use, taking into account
     * `WINEPREFIX` overrides and the default `~/.wine` fallback.
     */
    boost::filesystem::path normalize_wine_prefix() const;

    const PluginType plugin_type;

    /**
     * The path to our `.so` file. For VST3 plugins this is *not* the VST3
     * module (since that has to be bundle on Linux) but rather the .so file
     * contained in that bundle.
     */
    const boost::filesystem::path native_library_path;

   private:
    /**
     * The path to the Windows library (`.dll` or `.vst3`, not to be confused
     * with a `.vst3` bundle) that we're targeting. This should **not** be
     * passed to the plugin host and `windows_plugin_path` should be used
     * instead. We store this intermediate value so we can determine the
     * plugin's architecture.
     */
    const boost::filesystem::path windows_library_path;

   public:
    const LibArchitecture plugin_arch;

    /**
     * The path to the plugin (`.dll` or module) we're going to in the Wine
     * plugin host.
     *
     * For VST2 plugins this will be a `.dll` file. For VST3 plugins this is
     * normally a directory called `MyPlugin.vst3` that contains
     * `MyPlugin.vst3/Contents/x86-win/MyPlugin.vst3`, but there's also an older
     * deprecated (but still ubiquitous) format where the top level
     * `MyPlugin.vst3` is not a directory but a .dll file. This points to either
     * of those things, and then `VST3::Hosting::Win32Module::create()` will be
     * able to load it.
     *
     * https://developer.steinberg.help/pages/viewpage.action?pageId=9798275
     */
    const boost::filesystem::path windows_plugin_path;

    /**
     * The Wine prefix to use for hosting `windows_plugin_path`. If the
     * `WINEPREFIX` environment variable is set, then that will be used as an
     * override. Otherwise, we'll try to find the Wine prefix
     * `windows_plugin_path` is located in. The detection works by looking for a
     * directory containing a directory called `dosdevices`. If the plugin is
     * not inside of a Wine prefix, this will be left empty, and the default
     * prefix will be used instead.
     */
    const std::
        variant<OverridenWinePrefix, boost::filesystem::path, DefaultWinePrefix>
            wine_prefix;
};

/**
 * Returns equality for two strings when ignoring casing. Used for comparing
 * filenames inside of Wine prefixes since Windows/Wine does case folding for
 * filenames.
 */
bool equals_case_insensitive(const std::string& a, const std::string& b);

/**
 * Join a vector of strings with commas while wrapping the strings in quotes.
 * For example, `join_quoted_strings(std::vector<string>{"string", "another
 * string", "also a string"})` outputs `"'string', 'another string', 'also a
 * string'"`. This is used to format the initialisation message.
 */
std::string join_quoted_strings(std::vector<std::string>& strings);

/**
 * Create a logger prefix based on the endpoint base directory used for the
 * sockets for easy identification. This will result in a prefix of the form
 * `[<plugin_name>-<random_id>] `.
 *
 * @param endpoint_base_dir A directory name generated by
 *   `generate_endpoint_base()`.
 *
 * @return A prefix string for log messages.
 */
std::string create_logger_prefix(
    const boost::filesystem::path& endpoint_base_dir);

/**
 * Finds the Wine VST host (either `yabridge-host.exe` or `yabridge-host.exe`
 * depending on the plugin). For this we will search in two places:
 *
 *   1. Alongside libyabridge-{vst2,vst3}.so if the file got symlinked. This is
 *      useful when developing, as you can simply symlink the
 *      `libyabridge-{vst2,vst3}.so` file in the build directory without having
 *      to install anything to /usr.
 *   2. In the regular search path, augmented with `~/.local/share/yabridge` to
 *      ease the setup process.
 *
 * @param this_plugin_path The path to the `.so` file this code is being run
 *   from.
 * @param plugin_arch The architecture of the plugin, either 64-bit or 32-bit.
 *   Used to determine which host application to use, if available.
 * @param use_plugin_groups Whether the plugin is using plugin groups and we
 *   should be looking for the group host instead of the individual plugin host.
 *
 * @return The a path to the VST host, if found.
 * @throw std::runtime_error If the Wine VST host could not be found.
 *
 * TODO: Perhaps also move this somewhere else
 */
boost::filesystem::path find_vst_host(
    const boost::filesystem::path& this_plugin_path,
    LibArchitecture plugin_arch,
    bool use_plugin_groups);

/**
 * Generate the group socket endpoint name used based on the name of the group,
 * the Wine prefix in use and the plugin architecture. The resulting format is
 * in the form
 * `/run/user/<uid>/yabridge-group-<group_name>-<wine_prefix_id>-<architecture>.sock`.
 * In this socket name the `wine_prefix_id` is a numerical hash based on the
 * Wine prefix in use. This way the same group name can be used for multiple
 * Wine prefixes and for both 32 and 64 bit plugins without clashes.
 *
 * @param group_name The name of the plugin group.
 * @param wine_prefix The name of the Wine prefix in use. This should be
 *   obtained from `PluginInfo::normalize_wine_prefix()`.
 * @param architecture The architecture the plugin is using, since 64-bit
 *   processes can't host 32-bit plugins and the other way around.
 *
 * @return A socket endpoint path that corresponds to the format described
 *   above.
 */
boost::filesystem::path generate_group_endpoint(
    const std::string& group_name,
    const boost::filesystem::path& wine_prefix,
    const LibArchitecture architecture);

/**
 * Return the search path as defined in `$PATH`, with `~/.local/share/yabridge`
 * appended to the end. I'd rather not do this since more magic makes things
 * harder to comprehend, but I can understand that modifying your login shell's
 * `PATH` environment variable can be a big hurdle if you've never done anything
 * like that before. And since this is the recommended installation location, it
 * makes sense to also search there by default.
 */
std::vector<boost::filesystem::path> get_augmented_search_path();

/**
 * Return a path to this `.so` file. This can be used to find out from where
 * this link to or copy of `libyabridge-{vst2,vst3}.so` was loaded.
 */
boost::filesystem::path get_this_file_location();

/**
 * Return the installed Wine version. This is obtained by from `wine --version`
 * and then stripping the `wine-` prefix. This respects the `WINELOADER`
 * environment variable used in the scripts generated by winegcc.
 *
 * This will *not* throw when Wine can not be found, but will instead return
 * '<NOT FOUND>'. This way the user will still get some useful log files.
 */
std::string get_wine_version();

/**
 * Load the configuration that belongs to a copy of or symlink to
 * `libyabridge-{vst2,vst3}.so`. If no configuration file could be found then
 * this will return an empty configuration object with default settings. See the
 * docstrong on the `Configuration` class for more details on how to choose the
 * config file to load.
 *
 * This function will take any optional compile-time features that have not been
 * enabled into account.
 *
 * @param yabridge_path The path to the .so file that's being loaded.by the VST
 *   host. This will be used both for the starting location of the search and to
 *   determine which section in the config file to use.
 *
 * @return Either a configuration object populated with values from matched glob
 *   pattern within the found configuration file, or an empty configuration
 *   object if no configuration file could be found or if the plugin could not
 *   be matched to any of the glob patterns in the configuration file.
 *
 * @see Configuration
 */
Configuration load_config_for(const boost::filesystem::path& yabridge_path);

/**
 * Starting from the starting file or directory, go up in the directory
 * hierarchy until we find a file named `filename`.
 *
 * @param filename The name of the file we're looking for. This can also be a
 *   directory name since directories are also files.
 * @param starting_from The directory to start searching in. If this is a file,
 *   then start searching in the directory the file is located in.
 * @param predicate The predicate to use to check if the path matches a file.
 *   Needed as an easy way to limit the search to directories only since C++17
 *   does not have any built in coroutines or generators.
 *
 * @return The path to the *file* found, or `std::nullopt` if the file could not
 *   be found.
 */
template <typename F = bool(const boost::filesystem::path&)>
std::optional<boost::filesystem::path> find_dominating_file(
    const std::string& filename,
    boost::filesystem::path starting_dir,
    F predicate = boost::filesystem::exists) {
    while (starting_dir != "") {
        const boost::filesystem::path candidate = starting_dir / filename;
        if (predicate(candidate)) {
            return candidate;
        }

        starting_dir = starting_dir.parent_path();
    }

    return std::nullopt;
}
