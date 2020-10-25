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

#include <bitsery/adapter/buffer.h>
#include <bitsery/bitsery.h>

#ifdef __WINE__
#include "../wine-host/boost-fix.h"
#endif
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/filesystem.hpp>

template <typename B>
using OutputAdapter = bitsery::OutputBufferAdapter<B>;

template <typename B>
using InputAdapter = bitsery::InputBufferAdapter<B>;

/**
 * Manages all the sockets used for communicating between the plugin and the
 * Wine host. Every plugin will get its own directory (the socket endpoint base
 * directory), and all socket endpoints are created within this directory. This
 * is usually `/run/user/<uid>/yabridge-<plugin_name>-<random_id>/`.
 *
 * On the plugin side this class should be initialized with `listen` set to
 * `true` before launching the Wine VST host. This will start listening on the
 * sockets, and the call to `connect()` will then accept any incoming
 * connections.
 */
class Sockets {
   public:
    /**
     * Sets up the sockets using the specified base directory. The sockets won't
     * be active until `connect()` gets called.
     *
     * @param io_context The IO context the sockets should be bound to. Relevant
     *   when doing asynchronous operations.
     * @param endpoint_base_dir The base directory that will be used for the
     * Unix domain sockets.
     * @param listen If `true`, start listening on the sockets. Incoming
     *   connections will be accepted when `connect()` gets called. This should
     *   be set to `true` on the plugin side, and `false` on the Wine host side.
     *
     * @see Sockets::connect
     */
    Sockets(boost::asio::io_context& io_context,
            const boost::filesystem::path& endpoint_base_dir,
            bool listen);

    /**
     * Cleans up the directory containing the socket endpoints when yabridge
     * shuts down if it still exists.
     */
    ~Sockets();

    /**
     * Depending on the value of the `listen` argument passed to the
     * constructor, either accept connections made to the sockets on the Linux
     * side or connect to the sockets on the Wine side
     */
    void connect();

    /**
     * The base directory for our socket endpoints. All `*_endpoint` variables
     * below are files within this directory.
     */
    const boost::filesystem::path base_dir;

    /**
     * The IO context that the sockets will be created on. This is only relevant
     * for asynchronous operations.
     */
    boost::asio::io_context& io_context;

    // The naming convention for these sockets is `<from>_<to>_<event>`. For
    // instance the socket named `host_vst_dispatch` forwards
    // `AEffect.dispatch()` calls from the native VST host to the Windows VST
    // plugin (through the Wine VST host).

    /**
     * The socket that forwards all `dispatcher()` calls from the VST host to
     * the plugin.
     */
    boost::asio::local::stream_protocol::socket host_vst_dispatch;
    /**
     * Used specifically for the `effProcessEvents` opcode. This is needed
     * because the Win32 API is designed to block during certain GUI
     * interactions such as resizing a window or opening a dropdown. Without
     * this MIDI input would just stop working at times.
     */
    boost::asio::local::stream_protocol::socket host_vst_dispatch_midi_events;
    /**
     * The socket that forwards all `audioMaster()` calls from the Windows VST
     * plugin to the host.
     */
    boost::asio::local::stream_protocol::socket vst_host_callback;
    /**
     * Used for both `getParameter` and `setParameter` since they mostly
     * overlap.
     */
    boost::asio::local::stream_protocol::socket host_vst_parameters;
    /**
     * Used for processing audio usign the `process()`, `processReplacing()` and
     * `processDoubleReplacing()` functions.
     */
    boost::asio::local::stream_protocol::socket host_vst_process_replacing;
    /**
     * A control socket that sends data that is not suitable for the other
     * sockets. At the moment this is only used to, on startup, send the Windows
     * VST plugin's `AEffect` object to the native VST plugin, and to then send
     * the configuration (from `config`) back to the Wine host.
     */
    boost::asio::local::stream_protocol::socket host_vst_control;

   private:
    const boost::asio::local::stream_protocol::endpoint
        host_vst_dispatch_endpoint;
    const boost::asio::local::stream_protocol::endpoint
        host_vst_dispatch_midi_events_endpoint;
    const boost::asio::local::stream_protocol::endpoint
        vst_host_callback_endpoint;
    const boost::asio::local::stream_protocol::endpoint
        host_vst_parameters_endpoint;
    const boost::asio::local::stream_protocol::endpoint
        host_vst_process_replacing_endpoint;
    const boost::asio::local::stream_protocol::endpoint
        host_vst_control_endpoint;

    /**
     * All of our socket acceptors. We have to create these before launching the
     * Wine process.
     */
    struct Acceptors {
        boost::asio::local::stream_protocol::acceptor host_vst_dispatch;
        boost::asio::local::stream_protocol::acceptor
            host_vst_dispatch_midi_events;
        boost::asio::local::stream_protocol::acceptor vst_host_callback;
        boost::asio::local::stream_protocol::acceptor host_vst_parameters;
        boost::asio::local::stream_protocol::acceptor
            host_vst_process_replacing;
        boost::asio::local::stream_protocol::acceptor host_vst_control;
    };

    /**
     * If the `listen` constructor argument was set to `true`, when we'll
     * prepare a set of socket acceptors that listen on the socket endpoints.
     */
    std::optional<Acceptors> acceptors;
};

/**
 * Generate a unique base directory that can be used as a prefix for all Unix
 * domain socket endpoints used in `PluginBridge`/`Vst2Bridge`. This will
 * usually return `/run/user/<uid>/yabridge-<plugin_name>-<random_id>/`.
 *
 * Sockets for group hosts are handled separately. See
 * `../plugin/utils.h:generate_group_endpoint` for more information on those.
 *
 * @param plugin_name The name of the plugin we're generating endpoints for.
 *   Used as a visual indication of what plugin is using this endpoint.
 */
boost::filesystem::path generate_endpoint_base(const std::string& plugin_name);

/**
 * Serialize an object using bitsery and write it to a socket. This will write
 * both the size of the serialized object and the object itself over the socket.
 *
 * @param socket The Boost.Asio socket to write to.
 * @param object The object to write to the stream.
 * @param buffer The buffer to write to. This is useful for sending audio and
 *   chunk data since that can vary in size by a lot.
 *
 * @warning This operation is not atomic, and calling this function with the
 *   same socket from multiple threads at once will cause issues with the
 *   packets arriving out of order.
 *
 * @relates read_object
 */
template <typename T, typename Socket>
inline void write_object(
    Socket& socket,
    const T& object,
    std::vector<uint8_t> buffer = std::vector<uint8_t>(64)) {
    const size_t size =
        bitsery::quickSerialization<OutputAdapter<std::vector<uint8_t>>>(
            buffer, object);

    // Tell the other side how large the object is so it can prepare a buffer
    // large enough before sending the data
    // NOTE: We're writing these sizes as a 64 bit integers, **not** as pointer
    //       sized integers. This is to provide compatibility with the 32-bit
    //       bit bridge. This won't make any function difference aside from the
    //       32-bit host application having to convert between 64 and 32 bit
    //       integers.
    boost::asio::write(socket,
                       boost::asio::buffer(std::array<uint64_t, 1>{size}));
    const size_t bytes_written =
        boost::asio::write(socket, boost::asio::buffer(buffer, size));
    assert(bytes_written == size);
}

/**
 * Deserialize an object by reading it from a socket. This should be used
 * together with `write_object`. This will block until the object is available.
 *
 * @param socket The Boost.Asio socket to read from.
 * @param buffer The buffer to read into. This is useful for sending audio and
 *   chunk data since that can vary in size by a lot.
 *
 * @return The deserialized object.
 *
 * @throw std::runtime_error If the conversion to an object was not successful.
 *
 * @relates write_object
 */
template <typename T, typename Socket>
inline T read_object(Socket& socket,
                     std::vector<uint8_t> buffer = std::vector<uint8_t>(64)) {
    // See the note above on the use of `uint64_t` instead of `size_t`
    std::array<uint64_t, 1> message_length;
    boost::asio::read(socket, boost::asio::buffer(message_length));

    // Make sure the buffer is large enough
    const size_t size = message_length[0];
    buffer.resize(size);

    // `boost::asio::read/write` will handle all the packet splitting and
    // merging for us, since local domain sockets have packet limits somewhere
    // in the hundreds of kilobytes
    const auto actual_size =
        boost::asio::read(socket, boost::asio::buffer(buffer));
    assert(size == actual_size);

    T object;
    auto [_, success] =
        bitsery::quickDeserialization<InputAdapter<std::vector<uint8_t>>>(
            {buffer.begin(), size}, object);

    if (BOOST_UNLIKELY(!success)) {
        throw std::runtime_error("Deserialization failure in call:" +
                                 std::string(__PRETTY_FUNCTION__));
    }

    return object;
}
