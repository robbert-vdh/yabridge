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
inline void write_object(Socket& socket,
                         const T& object,
                         std::vector<uint8_t>& buffer) {
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
 * `write_object()` with a small default buffer for convenience.
 *
 * @overload
 */
template <typename T, typename Socket>
inline void write_object(Socket& socket, const T& object) {
    std::vector<uint8_t> buffer(64);
    write_object(socket, object, buffer);
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
 * @throw boost::system::system_error If the socket is closed or gets closed
 *   while reading.
 *
 * @relates write_object
 */
template <typename T, typename Socket>
inline T read_object(Socket& socket, std::vector<uint8_t>& buffer) {
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
        throw std::runtime_error("Deserialization failure in call: " +
                                 std::string(__PRETTY_FUNCTION__));
    }

    return object;
}

/**
 * `read_object()` with a small default buffer for convenience.
 *
 * @overload
 */
template <typename T, typename Socket>
inline T read_object(Socket& socket) {
    std::vector<uint8_t> buffer(64);
    return read_object<T>(socket, buffer);
}

/**
 * A single, long-living socket
 */
class SocketHandler {
   public:
    /**
     * Sets up the sockets and start listening on the socket on the listening
     * side. The sockets won't be active until `connect()` gets called.
     *
     * @param io_context The IO context the socket should be bound to.
     * @param endpoint The endpoint this socket should connect to or listen on.
     * @param listen If `true`, start listening on the sockets. Incoming
     *   connections will be accepted when `connect()` gets called. This should
     *   be set to `true` on the plugin side, and `false` on the Wine host side.
     *
     * @see Sockets::connect
     */
    SocketHandler(boost::asio::io_context& io_context,
                  boost::asio::local::stream_protocol::endpoint endpoint,
                  bool listen)
        : endpoint(endpoint), socket(io_context) {
        if (listen) {
            boost::filesystem::create_directories(
                boost::filesystem::path(endpoint.path()).parent_path());
            acceptor.emplace(io_context, endpoint);
        }
    }

    /**
     * Depending on the value of the `listen` argument passed to the
     * constructor, either accept connections made to the sockets on the Linux
     * side or connect to the sockets on the Wine side.
     */
    void connect() {
        if (acceptor) {
            acceptor->accept(socket);
        } else {
            socket.connect(endpoint);
        }
    }

    /**
     * Close the socket. Both sides that are actively listening will be thrown a
     * `boost::system_error` when this happens.
     */
    void close() {
        // The shutdown can fail when the socket is already closed
        boost::system::error_code err;
        socket.shutdown(
            boost::asio::local::stream_protocol::socket::shutdown_both, err);
        socket.close();
    }

    /**
     * Serialize an object and send it over the socket.
     *
     * @param object The object to send.
     * @param buffer The buffer to use for the serialization. This is used to
     *   prevent excess allocations when sending audio.
     *
     * @throw boost::system::system_error If the socket is closed or gets closed
     *   during sending.
     *
     * @warning This operation is not atomic, and calling this function with the
     *   same socket from multiple threads at once will cause issues with the
     *   packets arriving out of order.
     *
     * @see write_object
     * @see SocketHandler::receive_single
     * @see SocketHandler::receive_multi
     */
    template <typename T>
    inline void send(const T& object, std::vector<uint8_t>& buffer) {
        write_object(socket, object, buffer);
    }

    /**
     * `SocketHandler::send()` with a small default buffer for convenience.
     *
     * @overload
     */
    template <typename T>
    inline void send(const T& object) {
        write_object(socket, object);
    }

    /**
     * Read a serialized object from the socket sent using `send()`. This will
     * block until the object is available.
     *
     * @param buffer The buffer to read into. This is used to prevent excess
     *   allocations when sending audio.
     *
     * @return The deserialized object.
     *
     * @throw std::runtime_error If the conversion to an object was not
     *   successful.
     * @throw boost::system::system_error If the socket is closed or gets closed
     *   while reading.
     *
     * @relates SocketHandler::send
     *
     * @see read_object
     * @see SocketHandler::receive_multi
     */
    template <typename T>
    inline T receive_single(std::vector<uint8_t>& buffer) {
        return read_object<T>(socket, buffer);
    }

    /**
     * `SocketHandler::receive_single()` with a small default buffer for
     * convenience.
     *
     * @overload
     */
    template <typename T>
    inline T receive_single() {
        return read_object<T>(socket);
    }

    /**
     * Start a blocking loop to receive objects on this socket. This function
     * will return once the socket gets closed.
     *
     * @param callback A function that gets passed the received object. Since
     *   we'd probably want to do some more stuff after sending a reply, calling
     *   `send()` is the responsibility of this function.
     *
     * @tparam F A function type in the form of `void(T, std::vector<uint8_t>&)`
     *   that does something with the object, and then calls `send()`. The
     *   reading/writing buffer is passed along so it can be reused for sending
     *   large amounts of data.
     *
     * @relates SocketHandler::send
     *
     * @see read_object
     * @see SocketHandler::receive_single
     */
    template <typename T, typename F>
    void receive_multi(F callback) {
        std::vector<uint8_t> buffer{};
        while (true) {
            try {
                auto object = receive_single<T>(buffer);

                callback(std::move(object), buffer);
            } catch (const boost::system::system_error&) {
                // This happens when the sockets got closed because the plugin
                // is being shut down
                break;
            }
        }
    }

   private:
    boost::asio::local::stream_protocol::endpoint endpoint;
    boost::asio::local::stream_protocol::socket socket;

    /**
     * Will be used in `connect()` on the listening side to establish the
     * connection.
     */
    std::optional<boost::asio::local::stream_protocol::acceptor> acceptor;
};

/**
 * Generate a unique base directory that can be used as a prefix for all Unix
 * domain socket endpoints used in `Vst2PluginBridge`/`Vst2Bridge`. This will
 * usually return `/run/user/<uid>/yabridge-<plugin_name>-<random_id>/`.
 *
 * Sockets for group hosts are handled separately. See
 * `../plugin/utils.h:generate_group_endpoint` for more information on those.
 *
 * @param plugin_name The name of the plugin we're generating endpoints for.
 *   Used as a visual indication of what plugin is using this endpoint.
 */
boost::filesystem::path generate_endpoint_base(const std::string& plugin_name);
