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

// Generated inside of the build directory
#include <src/common/config/config.h>
#include <src/common/config/version.h>

#include "../../common/configuration.h"
#include "../../common/utils.h"
#include "../host-process.h"

/**
 * Handles all common operations for hosting plugins such as setting up the
 * plugin host process, the logger, and logging debug information on startup.
 *
 * @tparam Sockets the `Sockets` implementation to use. We have to initialize it
 *   here because we need to pass it to our `HostProcess`.
 */
template <std::derived_from<Sockets> TSockets>
class PluginBridge {
   public:
    /**
     * Sets up everything needed to start the host process. Classes deriving
     * from this should call `log_init_message()` themselves after their
     * initialization list.
     *
     * @param plugin_type The type of the plugin we're handling.
     * @param plugin_path The path to the plugin. For VST2 plugins this is the
     *   path to the `.dll` file, and for VST3 plugins this is the path to the
     *   module (either a `.vst3` DLL file or a bundle).
     * @param create_socket_instance A function to create a socket instance.
     *   Using a lambda here feels wrong, but I can't think of a better
     *   solution right now.
     */
    template <typename F>
    PluginBridge(PluginType plugin_type,
                 const boost::filesystem::path& plugin_path,
                 F create_socket_instance)
        : plugin_type(plugin_type),
          plugin_path(plugin_path),
          io_context(),
          sockets(create_socket_instance(io_context)),
          config(load_config_for(get_this_file_location())),
          generic_logger(Logger::create_from_environment(
              create_logger_prefix(sockets.base_dir))),
          plugin_host(
              config.group
                  ? std::unique_ptr<HostProcess>(std::make_unique<GroupHost>(
                        io_context,
                        generic_logger,
                        HostRequest{
                            .plugin_type = plugin_type,
                            .plugin_path = plugin_path.string(),
                            .endpoint_base_dir = sockets.base_dir.string()},
                        sockets,
                        *config.group))
                  : std::unique_ptr<HostProcess>(
                        std::make_unique<IndividualHost>(
                            io_context,
                            generic_logger,
                            HostRequest{.plugin_type = plugin_type,
                                        .plugin_path = plugin_path.string(),
                                        .endpoint_base_dir =
                                            sockets.base_dir.string()}))),
          has_realtime_priority(set_realtime_priority()),
          wine_io_handler([&]() { io_context.run(); }) {}

    virtual ~PluginBridge(){};

   protected:
    /**
     * Format and log all relevant debug information during initialization.
     */
    void log_init_message() {
        std::stringstream init_msg;

        init_msg << "Initializing yabridge version " << yabridge_git_version
                 << std::endl;
        init_msg << "host:         '" << plugin_host->path().string() << "'"
                 << std::endl;
        init_msg << "plugin:       '" << plugin_path.string() << "'"
                 << std::endl;
        init_msg << "plugin type:  '" << plugin_type_to_string(plugin_type)
                 << "'" << std::endl;
        init_msg << "realtime:     '" << (has_realtime_priority ? "yes" : "no")
                 << "'" << std::endl;
        init_msg << "sockets:      '" << sockets.base_dir.string() << "'"
                 << std::endl;
        init_msg << "wine prefix:  '";

        // If the Wine prefix is manually overridden, then this should be made
        // clear. This follows the behaviour of `set_wineprefix()`.
        boost::process::environment env = boost::this_process::environment();
        if (!env["WINEPREFIX"].empty()) {
            init_msg << env["WINEPREFIX"].to_string() << " <overridden>";
        } else {
            init_msg << find_wineprefix().value_or("<default>").string();
        }
        init_msg << "'" << std::endl;

        init_msg << "wine version: '" << get_wine_version() << "'" << std::endl;
        init_msg << std::endl;

        // Print the path to the currently loaded configuration file and all
        // settings in use. Printing the matched glob pattern could also be
        // useful but it'll be very noisy and it's likely going to be clear from
        // the shown values anyways.
        init_msg << "config from:   '"
                 << config.matched_file.value_or("<defaults>").string() << "'"
                 << std::endl;

        init_msg << "hosting mode:  '";
        if (config.group) {
            init_msg << "plugin group \"" << *config.group << "\"";
        } else {
            init_msg << "individually";
        }
        if (plugin_host->architecture() == LibArchitecture::dll_32) {
            init_msg << ", 32-bit";
        } else {
            init_msg << ", 64-bit";
        }
        init_msg << "'" << std::endl;

        init_msg << "other options: ";
        std::vector<std::string> other_options;
        if (config.cache_time_info) {
            other_options.push_back("hack: time info cache");
        }
        if (config.editor_double_embed) {
            other_options.push_back("editor: double embed");
        }
        if (!other_options.empty()) {
            init_msg << join_quoted_strings(other_options) << std::endl;
        } else {
            init_msg << "'<none>'" << std::endl;
        }

        // To make debugging easier, we'll print both unrecognized options (that
        // might be left over when an option gets removed) as well as options
        // have the wrong argument types
        if (!config.invalid_options.empty()) {
            init_msg << "invalid arguments: "
                     << join_quoted_strings(config.invalid_options)
                     << " (check the readme for more information)" << std::endl;
        }
        if (!config.unknown_options.empty()) {
            init_msg << "unrecognized options: "
                     << join_quoted_strings(config.unknown_options)
                     << std::endl;
        }
        init_msg << std::endl;

        // Include a list of enabled compile-tiem features, mostly to make debug
        // logs more useful
        init_msg << "Enabled features:" << std::endl;
#ifdef WITH_BITBRIDGE
        init_msg << "- bitbridge support" << std::endl;
#endif
#ifdef WITH_WINEDBG
        init_msg << "- winedbg" << std::endl;
#endif
#ifdef WITH_VST3
        init_msg << "- VST3 support" << std::endl;
#endif
#if !(defined(WITH_BITBRIDGE) || defined(WITH_WINEDBG) || defined(WITH_VST3))
        init_msg << "  <none>" << std::endl;
#endif
        init_msg << std::endl;

        for (std::string line = ""; std::getline(init_msg, line);) {
            generic_logger.log(line);
        }
    }

    /**
     * The type of the plugin we're dealing with. Passed to the host process and
     * printed in the initialisation message.
     */
    const PluginType plugin_type;

    /**
     * The path to the plugin (`.dll` or module) being loaded in the Wine plugin
     * host.
     */
    const boost::filesystem::path plugin_path;

    boost::asio::io_context io_context;

    /**
     * The sockets used for communication with the Wine process.
     *
     * @see PluginBridge::log_init_message
     */
    TSockets sockets;

    /**
     * The configuration for this instance of yabridge. Set based on the values
     * from a `yabridge.toml`, if it exists.
     *
     * @see ../utils.h:load_config_for
     */
    Configuration config;

    /**
     * The logging facility used for this instance of yabridge. See
     * `Logger::create_from_env()` for how this is configured.
     *
     * @see Logger::create_from_env
     */
    Logger generic_logger;

    /**
     * The Wine process hosting our plugins. In the case of group hosts a
     * `PluginBridge` instance doesn't actually own a process, but rather either
     * spawns a new detached process or it connects to an existing one.
     */
    std::unique_ptr<HostProcess> plugin_host;

    /**
     * Whether this process runs with realtime priority. We'll set this _after_
     * spawning the Wine process because from my testing running wineserver with
     * realtime priority can actually increase latency.
     */
    bool has_realtime_priority;

    /**
     * Runs the Boost.Asio `io_context` thread for logging the Wine process
     * STDOUT and STDERR messages.
     */
    std::jthread wine_io_handler;
};
