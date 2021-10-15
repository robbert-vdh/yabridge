// yabridge: a Wine VST bridge
// Copyright (C) 2020-2021 Robbert van der Helm
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

#include <iostream>
#include <mutex>

#include <bitsery/adapter/buffer.h>
#include <bitsery/bitsery.h>
#include <bitsery/traits/vector.h>

#ifdef __WINE__
#include "../wine-host/boost-fix.h"
#endif
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/filesystem.hpp>

#include "../bitsery/traits/small-vector.h"
#include "../logging/common.h"
#include "../utils.h"

// Our input and output adapters for binary serialization always expect the data
// to be encoded in little endian format. This should not make any difference
// currently, but this would make it possible (somewhat, it would probably still
// be too slow) to have yabridge be usable with Wine run through Qemu on
// big-endian architectures.
namespace bitsery {
struct LittleEndianConfig {
    // In case we ever want to bridge from some big-endian architecture to
    // x86_64 Windows, then we should make sure we always encode data using the
    // same endianness
    static constexpr EndiannessType Endianness = EndiannessType::LittleEndian;
    // Since we provide the data on both sides, we can safely disable these
    // checks
    static constexpr bool CheckDataErrors = false;
    static constexpr bool CheckAdapterErrors = false;
};
}  // namespace bitsery

template <typename B>
using OutputAdapter =
    bitsery::OutputBufferAdapter<B, bitsery::LittleEndianConfig>;

template <typename B>
using InputAdapter =
    bitsery::InputBufferAdapter<B, bitsery::LittleEndianConfig>;

/**
 * For binary serialization we use these small vectors that preallocate a small
 * capacity on the stack as part of our binary serialization process. For most
 * messages we don't need more than the default capacity (which would usually be
 * 64 bytes), so we can avoid a lot of allocations in the serialization process
 * this way.
 */
template <size_t N>
using SerializationBuffer = boost::container::small_vector<uint8_t, N>;

/**
 * The class `SerializationBuffer<N>` is derived from, so we can erase the
 * buffer's initial capacity from all functions that work with them.
 */
using SerializationBufferBase = boost::container::small_vector_base<uint8_t>;

namespace boost {
namespace asio {

template <typename PodType, typename Allocator>
inline BOOST_ASIO_MUTABLE_BUFFER buffer(
    boost::container::small_vector_base<PodType, Allocator>& data)
    BOOST_ASIO_NOEXCEPT {
    return BOOST_ASIO_MUTABLE_BUFFER(
        data.size() ? &data[0] : 0, data.size() * sizeof(PodType)
#if defined(BOOST_ASIO_ENABLE_BUFFER_DEBUGGING)
                                        ,
        detail::buffer_debug_check<typename boost::container::small_vector_base<
            PodType, Allocator>::iterator>(data.begin())
#endif  // BOOST_ASIO_ENABLE_BUFFER_DEBUGGING
    );
}

// These are copied verbatim `boost::asio::buffer(std::vector<PodType,
// Allocator>&, std::size_t)`, since `boost::container::small_vector` is
// compatible with the STL vector.
template <typename PodType, typename Allocator>
inline BOOST_ASIO_MUTABLE_BUFFER buffer(
    boost::container::small_vector_base<PodType, Allocator>& data,
    std::size_t max_size_in_bytes) BOOST_ASIO_NOEXCEPT {
    return BOOST_ASIO_MUTABLE_BUFFER(
        data.size() ? &data[0] : 0,
        data.size() * sizeof(PodType) < max_size_in_bytes
            ? data.size() * sizeof(PodType)
            : max_size_in_bytes
#if defined(BOOST_ASIO_ENABLE_BUFFER_DEBUGGING)
        ,
        detail::buffer_debug_check<typename boost::container::small_vector_base<
            PodType, Allocator>::iterator>(data.begin())
#endif  // BOOST_ASIO_ENABLE_BUFFER_DEBUGGING
    );
}

}  // namespace asio
}  // namespace boost

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
                         SerializationBufferBase& buffer) {
    const size_t size =
        bitsery::quickSerialization<OutputAdapter<SerializationBufferBase>>(
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
    SerializationBuffer<256> buffer{};
    write_object(socket, object, buffer);
}

/**
 * Deserialize an object by reading it from a socket. This should be used
 * together with `write_object`. This will block until the object is available.
 *
 * @param socket The Boost.Asio socket to read from.
 * @param object The object to serialize into. There are also overrides that
 *   create a new default initialized `T`
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
inline T& read_object(Socket& socket,
                      T& object,
                      SerializationBufferBase& buffer) {
    // See the note above on the use of `uint64_t` instead of `size_t`
    std::array<uint64_t, 1> message_length;
    boost::asio::read(socket, boost::asio::buffer(message_length),
                      boost::asio::transfer_exactly(sizeof(message_length)));

    // Make sure the buffer is large enough
    const size_t size = message_length[0];
    buffer.resize(size);

    // `boost::asio::read/write` will handle all the packet splitting and
    // merging for us, since local domain sockets have packet limits somewhere
    // in the hundreds of kilobytes
    boost::asio::read(socket, boost::asio::buffer(buffer),
                      boost::asio::transfer_exactly(size));

    auto [_, success] =
        bitsery::quickDeserialization<InputAdapter<SerializationBufferBase>>(
            {buffer.begin(), size}, object);

    if (!success) [[unlikely]] {
        throw std::runtime_error("Deserialization failure in call: " +
                                 std::string(__PRETTY_FUNCTION__));
    }

    return object;
}

/**
 * `read_object()` into a new default initialized object with an existing
 * buffer.
 *
 * @overload
 */
template <typename T, typename Socket>
inline T read_object(Socket& socket, SerializationBufferBase& buffer) {
    T object;
    read_object<T>(socket, object, buffer);

    return object;
}

/**
 * `read_object()` into an existing object a small default
 * buffer for convenience.
 *
 * @overload
 */
template <typename T, typename Socket>
inline T& read_object(Socket& socket, T& object) {
    SerializationBuffer<256> buffer{};
    return read_object<T>(socket, object, buffer);
}

/**
 * `read_object()` into a new default initialized object with a small default
 * buffer for convenience.
 *
 * @overload
 */
template <typename T, typename Socket>
inline T read_object(Socket& socket) {
    T object;
    SerializationBuffer<256> buffer{};
    read_object<T>(socket, object, buffer);

    return object;
}

/**
 * Generate a unique base directory that can be used as a prefix for all Unix
 * domain socket endpoints used in `Vst2PluginBridge`/`Vst2Bridge`. This will
 * usually return `/run/user/<uid>/yabridge/<plugin_name>-<random_id>/`.
 *
 * Sockets for group hosts are handled separately. See
 * `../plugin/utils.h:generate_group_endpoint` for more information on those.
 *
 * @param plugin_name The name of the plugin we're generating endpoints for.
 *   Used as a visual indication of what plugin is using this endpoint.
 */
boost::filesystem::path generate_endpoint_base(const std::string& plugin_name);

/**
 * Manages all the sockets used for communicating between the plugin and the
 * Wine host. Every plugin will get its own directory (the socket endpoint base
 * directory), and all socket endpoints are created within this directory. This
 * is usually `/run/user/<uid>/yabridge/<plugin_name>-<random_id>/`.
 */
class Sockets {
   public:
    /**
     * Sets up the the base directory for the sockets. Classes inheriting this
     * should set up their sockets here.
     *
     * @param endpoint_base_dir The base directory that will be used for the
     *   Unix domain sockets.
     *
     * @see Sockets::connect
     */
    Sockets(const boost::filesystem::path& endpoint_base_dir)
        : base_dir(endpoint_base_dir) {}

    /**
     * Shuts down and closes all sockets and then cleans up the directory
     * containing the socket endpoints when yabridge shuts down if it still
     * exists.
     *
     * @note Classes overriding this should call `close()` in their destructor.
     */
    virtual ~Sockets() noexcept {
        try {
            // NOTE: Because someone has wiped their home directory in the past
            //       by manually modifying the socket base directory argument
            //       for `yabridge-host.exe` to point to their home directory
            //       there's now a safeguard against that very thing. Hopefully
            //       this should never be needed, but if it is, then I'm glad
            //       we'll have it!
            const boost::filesystem::path temp_dir = get_temporary_directory();
            if (base_dir.string().starts_with(temp_dir.string())) {
                boost::filesystem::remove_all(base_dir);
            } else {
                Logger logger = Logger::create_exception_logger();

                logger.log("");
                logger.log("WARNING: Unexpected socket base directory found,");
                logger.log("         not removing '" + base_dir.string() + "'");
                logger.log("");
            }
        } catch (const boost::filesystem::filesystem_error&) {
            // There should not be any filesystem errors since only one side
            // removes the files, but if we somehow can't delete the file
            // then we can just silently ignore this
        } catch (const std::bad_alloc&) {
            // If we cannot clean up because we're out of memory, then that's
            // fine
        }
    }

    /**
     * Depending on the value of the `listen` argument passed to the
     * constructor, either accept connections made to the sockets on the Linux
     * side or connect to the sockets on the Wine side.
     *
     * @remark On the plugin side `PluginBridge::connect_sockets_guarded()`
     *   should be used instead so we can terminate everything in the event that
     *   Wine fails to start.
     */
    virtual void connect() = 0;

    /**
     * Shut down and close all sockets. Called during the destructor and also
     * explicitly called when shutting down a plugin in a group host process.
     *
     * It should be safe to call this function more than once, and it should be
     * called in the overridden class's destructor.
     */
    virtual void close() = 0;

    /**
     * The base directory for our socket endpoints. All `*_endpoint` variables
     * below are files within this directory.
     */
    const boost::filesystem::path base_dir;
};

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
     *   packets arriving out of order. The caller is responsible for preventing
     *   this.
     *
     * @see write_object
     * @see SocketHandler::receive_single
     * @see SocketHandler::receive_multi
     */
    template <typename T>
    inline void send(const T& object, SerializationBufferBase& buffer) {
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
     * @param object The object to serialize into. There are also overrides that
     *   create a new default initialized `T`
     * @param buffer The buffer to read into. This is useful for sending audio
     *   and chunk data since that can vary in size by a lot.
     *
     * @return The deserialized object.
     *
     * @throw std::runtime_error If the conversion to an object was not
     *   successful.
     * @throw boost::system::system_error If the socket is closed or gets closed
     *   while reading.
     *
     * @note This function can safely be called within the lambda of
     *   `SocketHandler::receive_multi()`.
     *
     * @warning This operation is not atomic, and calling this function with the
     *   same socket from multiple threads at once will cause issues with the
     *   packets arriving out of order. The caller is responsible for preventing
     *   this.
     *
     * @relates SocketHandler::send
     *
     * @see read_object
     * @see SocketHandler::receive_multi
     */
    template <typename T>
    inline T& receive_single(T& object, SerializationBufferBase& buffer) {
        return read_object<T>(socket, object, buffer);
    }

    /**
     * `SocketHandler::receive_single()` into a new default initialized object
     * with a small default buffer for convenience.
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
     * @tparam F A function type in the form of `void(T,
     *   SerializationBufferBase&)` that does something with the object, and
     *   then calls `send()`. The reading/writing buffer is passed along so it
     *   can be reused for sending large amounts of data.
     *
     * @relates SocketHandler::send
     *
     * @see read_object
     * @see SocketHandler::receive_single
     */
    template <typename T, std::invocable<T&, SerializationBufferBase&> F>
    void receive_multi(F&& callback) {
        SerializationBuffer<256> buffer{};
        T object;
        while (true) {
            try {
                receive_single<T>(object, buffer);

                callback(object, buffer);
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
 * There are situations where we can not know in advance how many sockets we
 * need. The main example of this are VST2 `dispatcher()` and `audioMaster()`
 * calls. These functions can be called from multiple threads at the same time,
 * so using a single socket with a mutex to prevent two threads from using the
 * socket at the same time would cause issues. Luckily situation does not come
 * up that often so to work around it, we'll do two things:
 *
 * - We'll keep a single long lived socket connection. This works the exact same
 *   way as every other `SocketHandler` socket. When we want to send data and
 *   the socket is primary socket is not currently being written to, we'll just
 *   use that. On the listening side we'll read from this in a loop.
 * - On the listening side we also have a second thread asynchronously listening
 *   for new connections on the socket endpoint. When the sending side wants to
 *   send data and the primary socket is in use, it will instantiate a new
 *   connection to same socket endpoint and it will send the data over that
 *   socket instead. On the listening side the new connection will be accepted,
 *   and a newly spawned thread will handle incoming connection just like it
 *   would for the primary socket.
 *
 * @tparam Thread The thread implementation to use. On the Linux side this
 *   should be `std::jthread` and on the Wine side this should be `Win32Thread`.
 */
template <typename Thread>
class AdHocSocketHandler {
   protected:
    /**
     * Sets up a single primary socket. The sockets won't be active until
     * `connect()` gets called.
     *
     * @param io_context The IO context the primary socket should be bound to. A
     *   new IO context will be created for accepting the additional incoming
     *   connections.
     * @param endpoint The socket endpoint used for this event handler.
     * @param listen If `true`, start listening on the sockets. Incoming
     *   connections will be accepted when `connect()` gets called. This should
     *   be set to `true` on the plugin side, and `false` on the Wine host side.
     *
     * @see Sockets::connect
     */
    AdHocSocketHandler(boost::asio::io_context& io_context,
                       boost::asio::local::stream_protocol::endpoint endpoint,
                       bool listen)
        : io_context(io_context), endpoint(endpoint), socket(io_context) {
        if (listen) {
            boost::filesystem::create_directories(
                boost::filesystem::path(endpoint.path()).parent_path());
            acceptor.emplace(io_context, endpoint);
        }
    }

   public:
    /**
     * Depending on the value of the `listen` argument passed to the
     * constructor, either accept connections made to the sockets on the Linux
     * side or connect to the sockets on the Wine side
     */
    void connect() {
        if (acceptor) {
            acceptor->accept(socket);

            // As mentioned in `acceptor's` docstring, this acceptor will be
            // recreated in `receive_multi()` on another context, and
            // potentially on the other side of the connection in the case
            // where we're handling `vst_host_callback` VST2 events
            acceptor.reset();
            boost::filesystem::remove(endpoint.path());
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

        while (currently_listening) {
            // If another thread is currently calling `receive_multi()`, we'll
            // spinlock until that function has exited. We would otherwise get a
            // use-after-free when this object is destroyed from another thread.
        }
    }

   protected:
    /**
     * Serialize and send an event over a socket. This is used for both the host
     * -> plugin 'dispatch' events and the plugin -> host 'audioMaster' host
     * callbacks since they follow the same format. See one of those functions
     * for details on the parameters and return value of this function.
     *
     * As described above, if this function is currently being called from
     * another thread, then this will create a new socket connection and send
     * the event there instead.
     *
     * @param callback A function that will be called with a reference to a
     *   socket. This is either the primary `socket`, or a new ad hock socket if
     *   this function is currently being called from another thread.
     */
    template <std::invocable<boost::asio::local::stream_protocol::socket&> F>
    std::invoke_result_t<F, boost::asio::local::stream_protocol::socket&> send(
        F&& callback) {
        // A bit of template and constexpr nastiness to allow us to either
        // return a value from the callback (for when writing the response to a
        // new object) or to return void (when we deserialize into an existing
        // object)
        constexpr bool returns_void = std::is_void_v<std::invoke_result_t<
            F, boost::asio::local::stream_protocol::socket&>>;

        // XXX: Maybe at some point we should benchmark how often this
        //      ad hoc socket spawning mechanism gets used. If some hosts
        //      for instance consistently and repeatedly trigger this then
        //      we might be able to do some optimizations there.
        std::unique_lock lock(write_mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            // This was used to always block when sending the first message,
            // because the other side may not be listening for additional
            // connections yet
            if constexpr (returns_void) {
                callback(socket);
                sent_first_event = true;
            } else {
                auto result = callback(socket);
                sent_first_event = true;

                return result;
            }
        } else {
            try {
                boost::asio::local::stream_protocol::socket secondary_socket(
                    io_context);
                secondary_socket.connect(endpoint);

                return callback(secondary_socket);
            } catch (const boost::system::system_error&) {
                // So, what do we do when noone is listening on the endpoint
                // yet? This can happen with plugin groups when the Wine
                // host process does an `audioMaster()` call before the
                // plugin is listening. If that happens we'll fall back to a
                // synchronous request. This is not very pretty, so if
                // anyone can think of a better way to structure all of this
                // while still mainting a long living primary socket please
                // let me know.
                // Note that this should **only** be done before the call to
                // `connect()`. If we get here at any other point then it
                // means that the plugin side is no longer listening on the
                // sockets, and we should thus just exit.
                if (!sent_first_event) {
                    std::lock_guard lock(write_mutex);

                    if constexpr (returns_void) {
                        callback(socket);
                        sent_first_event = true;
                    } else {
                        auto result = callback(socket);
                        sent_first_event = true;

                        return result;
                    }
                } else {
                    // Rethrow the exception if the sockets we're not
                    // handling the specific case described above
                    throw;
                }
            }
        }
    }

    /**
     * Spawn a new thread to listen for extra connections to `endpoint`, and
     * then a blocking loop that handles incoming data from the primary
     * `socket`.
     *
     * @param logger A logger instance for logging connection errors. This
     *   should only be passed on the plugin side.
     * @param primary_callback A function that will do a single read cycle for
     *   the primary socket socket that should do a single read cycle. This is
     *   called in a loop so it shouldn't do any looping itself.
     * @param secondary_callback A function that will be called when we receive
     *   an incoming connection on a secondary socket. This would often do the
     *   same thing as `primary_callback`, but secondary sockets may need some
     *   different handling.
     */
    template <std::invocable<boost::asio::local::stream_protocol::socket&> F,
              std::invocable<boost::asio::local::stream_protocol::socket&> G>
    void receive_multi(std::optional<std::reference_wrapper<Logger>> logger,
                       F&& primary_callback,
                       G&& secondary_callback) {
        // We use this flag to have the `close()` function wait for the this
        // function to exit, to prevent use-after-frees when destroying this
        // object from another thread.
        assert(!currently_listening);
        currently_listening = true;

        // As described above we'll handle incoming requests for `socket` on
        // this thread. We'll also listen for incoming connections on `endpoint`
        // on another thread. For any incoming connection we'll spawn a new
        // thread to handle the request. When `socket` closes and this loop
        // breaks, the listener and any still active threads will be cleaned up
        // before this function exits.
        boost::asio::io_context secondary_context{};

        // The previous acceptor has already been shut down by
        // `AdHocSocketHandler::connect()`
        acceptor.emplace(secondary_context, endpoint);

        // This works the exact same was as `active_plugins` and
        // `next_plugin_id` in `GroupBridge`
        std::unordered_map<size_t, Thread> active_secondary_requests{};
        std::atomic_size_t next_request_id{};
        std::mutex active_secondary_requests_mutex{};
        accept_requests(
            *acceptor, logger,
            [&](boost::asio::local::stream_protocol::socket secondary_socket) {
                const size_t request_id = next_request_id.fetch_add(1);

                // We have to make sure to keep moving these sockets into the
                // threads that will handle them
                std::lock_guard lock(active_secondary_requests_mutex);
                active_secondary_requests[request_id] = Thread(
                    [&, request_id](boost::asio::local::stream_protocol::socket
                                        secondary_socket) {
                        secondary_callback(secondary_socket);

                        // When we have processed this request, we'll join the
                        // thread again with the thread that's handling
                        // `secondary_context`
                        boost::asio::post(secondary_context, [&, request_id]() {
                            std::lock_guard lock(
                                active_secondary_requests_mutex);

                            // The join is implicit because we're using
                            // `std::jthread`/`Win32Thread`
                            active_secondary_requests.erase(request_id);
                        });
                    },
                    std::move(secondary_socket));
            });

        Thread secondary_requests_handler([&]() {
            pthread_setname_np(pthread_self(), "adhoc-acceptor");

            secondary_context.run();
        });

        // Now we'll handle reads on the primary socket in a loop until the
        // socket shuts down
        while (true) {
            try {
                primary_callback(socket);
            } catch (const boost::system::system_error&) {
                // This happens when the sockets got closed because the plugin
                // is being shut down
                break;
            }
        }

        // After the primary socket gets terminated (during shutdown) we'll make
        // sure all outstanding jobs have been processed and then drop all work
        // from the IO context
        std::lock_guard lock(active_secondary_requests_mutex);
        secondary_context.stop();
        acceptor.reset();

        currently_listening = false;
    }

    /**
     * The same as the above, but with a single callback for incoming
     * connections on the primary socket and on secondary sockets.
     *
     * @overload
     */
    template <std::invocable<boost::asio::local::stream_protocol::socket&> F>
    void receive_multi(std::optional<std::reference_wrapper<Logger>> logger,
                       F&& callback) {
        receive_multi(logger, callback, std::forward<F>(callback));
    }

   private:
    /**
     * Used in `receive_multi()` to asynchronously listen for secondary socket
     * connections. After `callback()` returns this function will continue to be
     * called until the IO context gets stopped.
     *
     * @param acceptor The acceptor we will be listening on.
     * @param logger A logger instance for logging connection errors. This
     *   should only be passed on the plugin side.
     * @param callback A function that handles the new socket connection.
     */
    template <std::invocable<boost::asio::local::stream_protocol::socket> F>
    void accept_requests(
        boost::asio::local::stream_protocol::acceptor& acceptor,
        std::optional<std::reference_wrapper<Logger>> logger,
        F&& callback) {
        acceptor.async_accept(
            [&, logger, callback](
                const boost::system::error_code& error,
                boost::asio::local::stream_protocol::socket secondary_socket) {
                if (error.failed()) {
                    // On the Wine side it's expected that the primary socket
                    // connection will be dropped during shutdown, so we can
                    // silently ignore any related socket errors on the Wine
                    // side
                    if (logger) {
                        logger->get().log(
                            "Failure while accepting connections: " +
                            error.message());
                    }

                    return;
                }

                callback(std::move(secondary_socket));

                accept_requests(acceptor, logger, callback);
            });
    }

    /**
     * The main IO context. New sockets created during `send()` will be
     * bound to this context. In `receive_multi()` we'll create a new IO context
     * since we want to do all listening there on a dedicated thread.
     */
    boost::asio::io_context& io_context;

    boost::asio::local::stream_protocol::endpoint endpoint;
    boost::asio::local::stream_protocol::socket socket;

    /**
     * This acceptor will be used once synchronously on the listening side
     * during `Sockets::connect()`. When `AdHocSocketHandler::receive_multi()`
     * is then called, we'll recreate the acceptor to asynchronously listen for
     * new incoming socket connections on `endpoint` using. This is important,
     * because on the case of `Vst2Sockets`'s' `vst_host_callback` the acceptor
     * is first accepts an initial socket on the plugin side (like all sockets),
     * but all additional incoming connections of course have to be listened for
     * on the plugin side.
     */
    std::optional<boost::asio::local::stream_protocol::acceptor> acceptor;

    /**
     * After the socket gets closed, we do some cleanup at the end of
     * `receive_multi()`. To prevent use-after-frees, we should wait for this
     * function to exit when `close()`-ing a socket that's currently being
     * listened on. Since after closing the socket the thread should terminate
     * near instantly, we'll just do a spinlock here instead of using condition
     * variables.
     */
    std::atomic_bool currently_listening = false;

    /**
     * A mutex that locks the primary `socket`. If this is locked, then any new
     * events will be sent over a new socket instead.
     */
    std::mutex write_mutex;

    /**
     * Indicates whether or not the remove has processed an event we sent from
     * this side. When a Windows VST2 plugin performs a host callback in its
     * constructor, before the native plugin has had time to connect to the
     * sockets, we want it to always wait for the sockets to come online, but
     * this fallback behaviour should only happen during initialization.
     */
    std::atomic_bool sent_first_event = false;
};
