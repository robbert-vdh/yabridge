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

#include <atomic>

#include "common.h"

/**
 * Manages all the sockets used for communicating between the plugin and the
 * Wine host when hosting a VST3 plugin.
 *
 * On the plugin side this class should be initialized with `listen` set to
 * `true` before launching the Wine VST host. This will start listening on the
 * sockets, and the call to `connect()` will then accept any incoming
 * connections.
 *
 * TODO: I have no idea what the best approach here is yet, so this is very much
 *       subject to change
 *
 * @tparam Thread The thread implementation to use. On the Linux side this
 *   should be `std::jthread` and on the Wine side this should be `Win32Thread`.
 */
template <typename Thread>
class Vst3Sockets : public Sockets {
   public:
    /**
     * Sets up the sockets using the specified base directory. The sockets won't
     * be active until `connect()` gets called.
     *
     * @param io_context The IO context the sockets should be bound to. Relevant
     *   when doing asynchronous operations.
     * @param endpoint_base_dir The base directory that will be used for the
     *   Unix domain sockets.
     * @param listen If `true`, start listening on the sockets. Incoming
     *   connections will be accepted when `connect()` gets called. This should
     *   be set to `true` on the plugin side, and `false` on the Wine host side.
     *
     * @see Vst3Sockets::connect
     */
    Vst3Sockets(boost::asio::io_context& io_context,
                const boost::filesystem::path& endpoint_base_dir,
                bool listen)
        : Sockets(endpoint_base_dir) {}

    ~Vst3Sockets() { close(); }

    void connect() override {}

    void close() override {
        // Manually close all sockets so we break out of any blocking operations
        // that may still be active
    }

    // TODO: I still don't know if recursive callbacks are a thing in VST3. If
    //       not, then we should probably have two `AdHocSocketHandler`s per
    //       plugin instance (one for each direction, as with `dispatcher()` and
    //       `audioMaster()` in VST2). Using fewer probably also works, but we
    //       wouldn't want to have to spawn new sockets during audio processing.
};
