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

#include <boost/asio/io_context.hpp>
#include <mutex>
#include <thread>

#include "../../common/communication/vst3.h"
#include "../../common/configuration.h"
#include "../../common/logging/vst3.h"
#include "../host-process.h"

/**
 * This handles the communication between the native host and a VST3 plugin
 * hosted in our Wine plugin host. VST3 is handled very differently from VST2
 * because a plugin is no longer its own entity, but rather a definition of
 * objects that the host can create and interconnect. This `Vst3PluginBridge`
 * will be instantiated when the plugin first gets loaded, and it will survive
 * until the last instance of the plugin gets removed. The Wine host process
 * will thus also have the same lifetime, and even with yabridge's 'individual'
 * plugin hosting other instances of the same plugin will be handled by a single
 * process.
 *
 * @remark See the comments at the top of `vst3-plugin.cpp` for more
 *   information.
 *
 * The naming scheme of all of these 'bridge' classes is `<type>{,Plugin}Bridge`
 * for greppability reasons. The `Plugin` infix is added on the native plugin
 * side.
 */
class Vst3PluginBridge {
   public:
    /**
     * Initializes the VST3 module by starting and setting up communicating with
     * the Wine plugin host.
     *
     * @throw std::runtime_error Thrown when the Wine plugin host could not be
     *   found, or if it could not locate and load a VST3 module.
     */
    Vst3PluginBridge();

    /**
     * The configuration for this instance of yabridge. Set based on the values
     * from a `yabridge.toml`, if it exists.
     *
     * @see ../utils.h:load_config_for
     */
    Configuration config;

    /**
     * The path to the VST3 module being loaded in the Wine VST host. This is
     * normally a directory called `MyPlugin.vst3` that contains
     * `MyPlugin.vst3/Contents/x86-win/MyPlugin.vst3`, but there's also an older
     * deprecated (but still ubiquitous) format where the top level
     * `MyPlugin.vst3` is not a directory but a .dll file. This points to either
     * of those things, and then `VST3::Hosting::Win32Module::create()` will be
     * able to load it.
     *
     * https://developer.steinberg.help/pages/viewpage.action?pageId=9798275
     */
    const boost::filesystem::path plugin_module_path;

   private:
    /**
     * Format and log all relevant debug information during initialization.
     */
    void log_init_message();

    boost::asio::io_context io_context;
    Vst3Sockets<std::jthread> sockets;

    /**
     * The logging facility used for this instance of yabridge. See
     * `Logger::create_from_env()` for how this is configured.
     *
     * @see Logger::create_from_env
     */
    Vst3Logger logger;

    /**
     * The version of Wine currently in use. Used in the debug output on plugin
     * startup.
     */
    const std::string wine_version;

    /**
     * The Wine process hosting the Windows VST3 plugin.
     *
     * @see launch_vst_host
     */
    std::unique_ptr<HostProcess> vst_host;

    /**
     * A thread used during the initialisation process to terminate listening on
     * the sockets if the Wine process cannot start for whatever reason. This
     * has to be defined here instead of in the constructor we can't simply
     * detach the thread as it has to check whether the Wine plugin host is
     * still running.
     */
    std::jthread host_guard_handler;

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
