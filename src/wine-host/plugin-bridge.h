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

#include "native-includes.h"

// `native-includes.h` has to be included before any other files as otherwise we
// might get the wrong version of certain libraries
#define WIN32_LEAN_AND_MEAN
#include <vestige/aeffect.h>
#include <windows.h>

#include <thread>

#include "../common/communication.h"

/**
 * This handles the communication between the Linux native VST plugin and the
 * Wine VST host. The functions below should be used as callback functions in an
 * `AEffect` object.
 */
class PluginBridge {
   public:
    /**
     * Initializes the Windows VST plugin and set up communication with the
     * native Linux VST plugin.
     *
     * @param plugin_dll_path A (Unix style) path to the VST plugin .dll file to
     *   load.
     * @param socket_endpoint_path A (Unix style) path to the Unix socket
     *   endpoint the native VST plugin created to communicate over.
     *
     * @throw std::runtime_error Thrown when the VST plugin could not be loaded,
     *   or if communication could not be set up.
     */
    PluginBridge(std::string plugin_dll_path, std::string socket_endpoint_path);

    /**
     * Block the main thread until the plugin shuts down.
     */
    void wait();

    intptr_t host_callback(AEffect*, int32_t, int32_t, intptr_t, void*, float);

   private:
    /**
     * The shared library handle of the VST plugin. I sadly could not get
     * Boost.DLL to work here, so we'll just load the VST plugisn by hand.
     */
    std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&FreeLibrary)>
        plugin_handle;

    /**
     * The loaded plugin's `AEffect` struct, obtained using the above library
     * handle.
     */
    AEffect* plugin;

    boost::asio::io_context io_context;
    boost::asio::local::stream_protocol::endpoint socket_endpoint;

    // The naming convention for these sockets is `<from>_<to>_<event>`. For
    // instance the socket named `host_vst_dispatch` forwards
    // `AEffect.dispatch()` calls from the native VST host to the Windows VST
    // plugin (through the Wine VST host).
    boost::asio::local::stream_protocol::socket host_vst_dispatch;
    boost::asio::local::stream_protocol::socket vst_host_callback;
    /**
     * Used for both `getParameter` and `setParameter` since they mostly
     * overlap.
     */
    boost::asio::local::stream_protocol::socket host_vst_parameters;
    boost::asio::local::stream_protocol::socket host_vst_process_replacing;

    /**
     * This socket only handles updates of the `AEffect` struct instead of
     * passing through function calls. It's also used during initialization to
     * pass the Wine plugin's information to the host.
     */
    boost::asio::local::stream_protocol::socket vst_host_aeffect;

    /**
     * The thread that handles dispatch events from the host.
     */
    std::thread dispatch_handler;
    /**
     * The thread that responds to `getParameter` and `setParameter` requests.
     */
    std::thread parameters_handler;
    /**
     * The t thread that handles calls to `processReplacing` (and `process`).
     */
    std::thread process_replacing_handler;

    /**
     * A scratch buffer for sending and receiving data during `process` and
     * `processReplacing` calls.
     */
    std::unique_ptr<AudioBuffers::buffer_type> process_buffer;
};
