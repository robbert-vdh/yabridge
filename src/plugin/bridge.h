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

#include <vestige/aeffect.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/process/child.hpp>

/**
 * This handles the communication between the Linux native VST plugin and the
 * Wine VST host. The functions below should be used as callback functions in an
 * `AEffect` object.
 */
class Bridge {
   public:
    /**
     * Initializes the Wine VST bridge. This sets up the sockets for event
     * handling.
     *
     * TODO: Figure out whether shared memory gives us better throughput and/or
     *       lower overhead than using a Unix domain socket would.
     *
     * @throw std::runtime_error Thrown when the VST host could not be found, or
     *   if it could not locate and load a VST .dll file.
     */
    Bridge();

    // The four below functions are the handlers from the VST2 API. They are
    // called through proxy functions in `plugin.cpp`.

    /**
     * Handle an event sent by the VST host. Most of these opcodes will be
     * passed through to the winelib VST host.
     */
    intptr_t dispatch(AEffect* plugin,
                      int32_t opcode,
                      int32_t parameter,
                      intptr_t value,
                      void* data,
                      float option);
    void process(AEffect* plugin,
                 float** inputs,
                 float** outputs,
                 int32_t sample_frames);
    void set_parameter(AEffect* plugin, int32_t index, float value);
    float get_parameter(AEffect* plugin, int32_t index);

   private:
    boost::asio::io_context io_context;
    boost::asio::local::stream_protocol::endpoint socket_endpoint;
    boost::asio::local::stream_protocol::acceptor socket_acceptor;

    // The naming convention for these sockets is `<from>_<to>_<event>`. For
    // instance the socket named `host_vst_dispatch` forwards
    // `AEffect.dispatch()` calls from the native VST host to the Windows VST
    // plugin (through the Wine VST host).
    boost::asio::local::stream_protocol::socket host_vst_dispatch;

    boost::process::child vst_host;
};
