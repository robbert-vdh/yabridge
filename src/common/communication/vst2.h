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

#include "../logging.h"

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
 * Encodes the base behavior for reading from and writing to the `data` argument
 * for event dispatch functions. This provides base functionality for these
 * kinds of events. The `dispatch()` function will require some more specific
 * structs.
 */
class DefaultDataConverter {
   public:
    virtual ~DefaultDataConverter(){};

    /**
     * Read data from the `data` void pointer into a an `EventPayload` value
     * that can be serialized and conveys the meaning of the event.
     */
    virtual EventPayload read(const int opcode,
                              const int index,
                              const intptr_t value,
                              const void* data) const;

    /**
     * Read data from the `value` pointer into a an `EventPayload` value that
     * can be serialized and conveys the meaning of the event. This is only used
     * for the `effSetSpeakerArrangement` and `effGetSpeakerArrangement` events.
     */
    virtual std::optional<EventPayload> read_value(const int opcode,
                                                   const intptr_t value) const;

    /**
     * Write the reponse back to the `data` pointer.
     */
    virtual void write(const int opcode,
                       void* data,
                       const EventResult& response) const;

    /**
     * Write the reponse back to the `value` pointer. This is only used during
     * the `effGetSpeakerArrangement` event.
     */
    virtual void write_value(const int opcode,
                             intptr_t value,
                             const EventResult& response) const;

    /**
     * This function can override a callback's return value based on the opcode.
     * This is used in one place to return a pointer to a `VstTime` object
     * that's contantly being updated.
     *
     * @param opcode The opcode for the current event.
     * @param original The original return value as returned by the callback
     *   function.
     */
    virtual intptr_t return_value(const int opcode,
                                  const intptr_t original) const;
};

/**
 * So, this is a bit of a mess. The TL;DR is that we want to use a single long
 * living socket connection for `dispatch()` and another one for `audioMaster()`
 * for performance reasons, but when the socket is already being written to we
 * create new connections on demand.
 *
 * For most of our sockets we can just send out our messages on the writing
 * side, and do a simple blocking loop on the reading side. The `dispatch()` and
 * `audioMaster()` calls are different. Not only do they have they come with
 * complex payload values, they can also be called simultaneously from multiple
 * threads, and `audioMaster()` and `dispatch()` calls can even be mutually
 * recursive. Luckily this does not happen very often, but it does mean that our
 * simple 'one-socket-per-function' model doesn't work anymore. Because setting
 * up new sockets is quite expensive and this is seldom needed, this works
 * slightly differently:
 *
 * - We'll keep a single long lived socket connection. This works the exact same
 *   way as every other socket defined in the `Sockets` class.
 * - Aside from that the listening side will have a second thread asynchronously
 *   listening for new connections on the socket endpoint.
 *
 * The `EventHandler::send_event()` method is used to send events. If the socket
 * is currently being written to, we'll first create a new socket connection as
 * described above. Similarly, the `EventHandler::receive_events()` method first
 * sets up asynchronous listeners for the socket endpoint, and then block and
 * handle events until the main socket is closed.
 *
 * @tparam Thread The thread implementation to use. On the Linux side this
 *   should be `std::jthread` and on the Wine side this should be `Win32Thread`.
 */
template <typename Thread>
class EventHandler {
   public:
    /**
     * Sets up a single main socket for this type of events. The sockets won't
     * be active until `connect()` gets called.
     *
     * @param io_context The IO context the main socket should be bound to. A
     *   new IO context will be created for accepting the additional incoming
     *   connections.
     * @param endpoint The socket endpoint used for this event handler.
     * @param listen If `true`, start listening on the sockets. Incoming
     *   connections will be accepted when `connect()` gets called. This should
     *   be set to `true` on the plugin side, and `false` on the Wine host side.
     *
     * @see Sockets::connect
     */
    EventHandler(boost::asio::io_context& io_context,
                 boost::asio::local::stream_protocol::endpoint endpoint,
                 bool listen)
        : io_context(io_context), endpoint(endpoint), socket(io_context) {
        if (listen) {
            boost::filesystem::create_directories(
                boost::filesystem::path(endpoint.path()).parent_path());
            acceptor.emplace(io_context, endpoint);
        }
    }

    /**
     * Depending on the value of the `listen` argument passed to the
     * constructor, either accept connections made to the sockets on the Linux
     * side or connect to the sockets on the Wine side
     */
    void connect() {
        if (acceptor) {
            acceptor->accept(socket);

            // As mentioned in `acceptor's` docstring, this acceptor will be
            // recreated in `receive_events()` on another context, and
            // potentially on the other side of the connection in the case of
            // `vst_host_callback`
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
    }

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
     * @param data_converter Some struct that knows how to read data from and
     *   write data back to the `data` void pointer. For host callbacks this
     *   parameter contains either a string or a null pointer while `dispatch()`
     *   calls might contain opcode specific structs. See the documentation for
     *   `EventPayload` for more information. The `DefaultDataConverter` defined
     *   above handles the basic behavior that's sufficient for host callbacks.
     * @param logging A pair containing a logger instance and whether or not
     *   this is for sending `dispatch()` events or host callbacks. Optional
     *   since it doesn't have to be done on both sides.
     *
     * @relates EventHandler::receive_events
     * @relates passthrough_event
     */
    template <typename D>
    intptr_t send_event(D& data_converter,
                        std::optional<std::pair<Logger&, bool>> logging,
                        int opcode,
                        int index,
                        intptr_t value,
                        void* data,
                        float option) {
        // Encode the right payload types for this event. Check the
        // documentation for `EventPayload` for more information. These types
        // are converted to C-style data structures in `passthrough_event()` so
        // they can be passed to a plugin or callback function.
        const EventPayload payload =
            data_converter.read(opcode, index, value, data);
        const std::optional<EventPayload> value_payload =
            data_converter.read_value(opcode, value);

        if (logging) {
            auto [logger, is_dispatch] = *logging;
            logger.log_event(is_dispatch, opcode, index, value, payload, option,
                             value_payload);
        }

        const Event event{.opcode = opcode,
                          .index = index,
                          .value = value,
                          .option = option,
                          .payload = payload,
                          .value_payload = value_payload};

        // A socket only handles a single request at a time as to prevent
        // messages from arriving out of order. For throughput reasons we prefer
        // to do most communication over a single main socket (`socket`), and
        // we'll lock `write_mutex` while doing so. In the event that the mutex
        // is already locked and thus the main socket is currently in use by
        // another thread, then we'll spawn a new socket to handle the request.
        EventResult response;
        {
            std::unique_lock lock(write_mutex, std::try_to_lock);
            if (lock.owns_lock()) {
                write_object(socket, event);
                response = read_object<EventResult>(socket);
            } else {
                try {
                    boost::asio::local::stream_protocol::socket
                        secondary_socket(io_context);
                    secondary_socket.connect(endpoint);

                    write_object(secondary_socket, event);
                    response = read_object<EventResult>(secondary_socket);
                } catch (const boost::system::system_error&) {
                    // So, what do we do when noone is listening on the endpoint
                    // yet? This can happen with plugin groups when the Wine
                    // host process does an `audioMaster()` call before the
                    // plugin is listening. If that happens we'll fall back to a
                    // synchronous request. This is not very pretty, so if
                    // anyone can think of a better way to structure all of this
                    // while still mainting a long living primary socket please
                    // let me know.
                    std::lock_guard lock(write_mutex);

                    write_object(socket, event);
                    response = read_object<EventResult>(socket);
                }
            }
        }

        if (logging) {
            auto [logger, is_dispatch] = *logging;
            logger.log_event_response(is_dispatch, opcode,
                                      response.return_value, response.payload,
                                      response.value_payload);
        }

        data_converter.write(opcode, data, response);
        data_converter.write_value(opcode, value, response);

        return data_converter.return_value(opcode, response.return_value);
    }

    /**
     * Spawn a new thread to listen for extra connections to `endpoint`, and
     * then a blocking loop that handles events from the primary `socket`.
     *
     * The specified function will be used to create an `EventResult` from an
     * `Event`. This is almost uses `passthrough_event()`, which converts a
     * `EventPayload` into the format used by VST2, calls either `dispatch()` or
     * `audioMaster()` depending on the context, and then serializes the result
     * back into an `EventResultPayload`.
     *
     * This function will also be used separately for receiving MIDI data, as
     * some plugins will need pointers to received MIDI data to stay alive until
     * the next audio buffer gets processed.
     *
     * @param logging A pair containing a logger instance and whether or not
     *   this is for sending `dispatch()` events or host callbacks. Optional
     *   since it doesn't have to be done on both sides.
     * @param callback The function used to generate a response out of an event.
     *   See the definition of `F` for more information.
     *
     * @tparam F A function type in the form of `EventResponse(Event, bool)`.
     *   The boolean flag is `true` when this event was received on the main
     *   socket, and `false` otherwise.
     *
     * @relates EventHandler::send_event
     * @relates passthrough_event
     */
    template <typename F>
    void receive_events(std::optional<std::pair<Logger&, bool>> logging,
                        F callback) {
        // Reading, processing, and writing back event data from the sockets
        // works in the same way regardless of which socket we're using
        const auto process_event =
            [&](boost::asio::local::stream_protocol::socket& socket,
                bool on_main_thread) {
                auto event = read_object<Event>(socket);
                if (logging) {
                    auto [logger, is_dispatch] = *logging;
                    logger.log_event(is_dispatch, event.opcode, event.index,
                                     event.value, event.payload, event.option,
                                     event.value_payload);
                }

                EventResult response = callback(event, on_main_thread);
                if (logging) {
                    auto [logger, is_dispatch] = *logging;
                    logger.log_event_response(
                        is_dispatch, event.opcode, response.return_value,
                        response.payload, response.value_payload);
                }

                write_object(socket, response);
            };

        // As described above we'll handle incoming requests for `socket` on
        // this thread. We'll also listen for incoming connections on `endpoint`
        // on another thread. For any incoming connection we'll spawn a new
        // thread to handle the request. When `socket` closes and this loop
        // breaks, the listener and any still active threads will be cleaned up
        // before this function exits.
        boost::asio::io_context secondary_context{};

        // The previous acceptor has already been shut down by
        // `EventHandler::connect()`
        acceptor.emplace(secondary_context, endpoint);

        // This works the exact same was as `active_plugins` and
        // `next_plugin_id` in `GroupBridge`
        std::map<size_t, Thread> active_secondary_requests{};
        std::atomic_size_t next_request_id{};
        std::mutex active_secondary_requests_mutex{};
        accept_requests(
            *acceptor, logging,
            [&](boost::asio::local::stream_protocol::socket secondary_socket) {
                const size_t request_id = next_request_id.fetch_add(1);

                // We have to make sure to keep moving these sockets into the
                // threads that will handle them
                std::lock_guard lock(active_secondary_requests_mutex);
                active_secondary_requests[request_id] = Thread(
                    [&, request_id](boost::asio::local::stream_protocol::socket
                                        secondary_socket) {
                        process_event(secondary_socket, false);

                        // When we have processed this request, we'll join the
                        // thread again with the thread that's handling
                        // `secondary_context`.
                        boost::asio::post(secondary_context, [&, request_id]() {
                            std::lock_guard lock(
                                active_secondary_requests_mutex);

                            // The join is implicit because we're using
                            // std::jthread/Win32Thread
                            active_secondary_requests.erase(request_id);
                        });
                    },
                    std::move(secondary_socket));
            });

        Thread secondary_requests_handler([&]() { secondary_context.run(); });

        while (true) {
            try {
                process_event(socket, true);
            } catch (const boost::system::system_error&) {
                // This happens when the sockets got closed because the plugin
                // is being shut down
                break;
            }
        }

        // After the main socket gets terminated (during shutdown) we'll make
        // sure all outstanding jobs have been processed and then drop all work
        // from the IO context
        std::lock_guard lock(active_secondary_requests_mutex);
        secondary_context.stop();
        acceptor.reset();
    }

   private:
    /**
     * Used in `receive_events()` to asynchronously listen for secondary socket
     * connections. After `callback()` returns this function will continue to be
     * called until the IO context gets stopped.
     *
     * @param acceptor The acceptor we will be listening on.
     * @param logging A pair containing a logger instance and whether or not
     *   this is for sending `dispatch()` events or host callbacks. Optional
     *   since it doesn't have to be done on both sides.
     * @param callback A function that handles the new socket connection.
     *
     * @tparam F A function in the form
     *   `void(boost::asio::local::stream_protocol::socket)` to handle a new
     *   incoming connection.
     */
    template <typename F>
    void accept_requests(
        boost::asio::local::stream_protocol::acceptor& acceptor,
        std::optional<std::pair<Logger&, bool>> logging,
        F callback) {
        acceptor.async_accept(
            [&, logging, callback](
                const boost::system::error_code& error,
                boost::asio::local::stream_protocol::socket secondary_socket) {
                if (error.failed()) {
                    // On the Wine side it's expected that the main socket
                    // connection will be dropped during shutdown, so we can
                    // silently ignore any related socket errors on the Wine
                    // side
                    if (logging) {
                        auto [logger, is_dispatch] = *logging;
                        logger.log("Failure while accepting connections: " +
                                   error.message());
                    }

                    return;
                }

                callback(std::move(secondary_socket));

                accept_requests(acceptor, logging, callback);
            });
    }

    /**
     * The main IO context. New sockets created during `send_event()` will be
     * bound to this context. In `receive_events()` we'll create a new IO
     * context since we want to do all listening there on a dedicated thread.
     */
    boost::asio::io_context& io_context;

    boost::asio::local::stream_protocol::endpoint endpoint;
    boost::asio::local::stream_protocol::socket socket;

    /**
     * This acceptor will be used once synchronously on the listening side
     * during `Sockets::connect()`. When `EventHandler::receive_events()` is
     * then called, we'll recreate the acceptor to asynchronously listen for new
     * incoming socket connections on `endpoint` using. This is important,
     * because on the case of `vst_host_callback` the acceptor is first accepts
     * an initial socket on the plugin side (like all sockets), but all
     * additional incoming connections of course have to be listened for on the
     * plugin side.
     */
    std::optional<boost::asio::local::stream_protocol::acceptor> acceptor;

    /**
     * A mutex that locks the main `socket`. If this is locked, then any new
     * events will be sent over a new socket instead.
     */
    std::mutex write_mutex;
};

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
 *
 * @tparam Thread The thread implementation to use. On the Linux side this
 *   should be `std::jthread` and on the Wine side this should be `Win32Thread`.
 */
template <typename Thread>
class Sockets {
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
     * @see Sockets::connect
     */
    Sockets(boost::asio::io_context& io_context,
            const boost::filesystem::path& endpoint_base_dir,
            bool listen)
        : base_dir(endpoint_base_dir),
          host_vst_dispatch(io_context,
                            (base_dir / "host_vst_dispatch.sock").string(),
                            listen),
          vst_host_callback(io_context,
                            (base_dir / "vst_host_callback.sock").string(),
                            listen),
          host_vst_parameters(io_context,
                              (base_dir / "host_vst_parameters.sock").string(),
                              listen),
          host_vst_process_replacing(
              io_context,
              (base_dir / "host_vst_process_replacing.sock").string(),
              listen),
          host_vst_control(io_context,
                           (base_dir / "host_vst_control.sock").string(),
                           listen) {}

    /**
     * Cleans up the directory containing the socket endpoints when yabridge
     * shuts down if it still exists.
     */
    ~Sockets() {
        // Manually close all sockets so we break out of any blocking operations
        // that may still be active
        host_vst_dispatch.close();
        vst_host_callback.close();
        host_vst_parameters.close();
        host_vst_process_replacing.close();
        host_vst_control.close();

        // Only clean if we're the ones who have created these files, although
        // it should not cause any harm to also do this on the Wine side
        try {
            boost::filesystem::remove_all(base_dir);
        } catch (const boost::filesystem::filesystem_error&) {
            // There should not be any filesystem errors since only one side
            // removes the files, but if we somehow can't delete the file
            // then we can just silently ignore this
        }
    }

    /**
     * Depending on the value of the `listen` argument passed to the
     * constructor, either accept connections made to the sockets on the Linux
     * side or connect to the sockets on the Wine side
     */
    void connect() {
        host_vst_dispatch.connect();
        vst_host_callback.connect();
        host_vst_parameters.connect();
        host_vst_process_replacing.connect();
        host_vst_control.connect();
    }

    /**
     * The base directory for our socket endpoints. All `*_endpoint` variables
     * below are files within this directory.
     */
    const boost::filesystem::path base_dir;

    // The naming convention for these sockets is `<from>_<to>_<event>`. For
    // instance the socket named `host_vst_dispatch` forwards
    // `AEffect.dispatch()` calls from the native VST host to the Windows VST
    // plugin (through the Wine VST host).

    /**
     * The socket that forwards all `dispatcher()` calls from the VST host to
     * the plugin.
     */
    EventHandler<Thread> host_vst_dispatch;
    /**
     * The socket that forwards all `audioMaster()` calls from the Windows VST
     * plugin to the host.
     */
    EventHandler<Thread> vst_host_callback;
    /**
     * Used for both `getParameter` and `setParameter` since they mostly
     * overlap.
     */
    SocketHandler host_vst_parameters;
    /**
     * Used for processing audio usign the `process()`, `processReplacing()` and
     * `processDoubleReplacing()` functions.
     */
    SocketHandler host_vst_process_replacing;
    /**
     * A control socket that sends data that is not suitable for the other
     * sockets. At the moment this is only used to, on startup, send the Windows
     * VST plugin's `AEffect` object to the native VST plugin, and to then send
     * the configuration (from `config`) back to the Wine host.
     */
    SocketHandler host_vst_control;
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

/**
 * Unmarshall an `EventPayload` back to the representation used by VST2, pass
 * that value to a callback function (either `AEffect::dispatcher()` for host ->
 * plugin events or `audioMaster()` for plugin -> host events), and then
 * serialize the results back into an `EventResult`.
 *
 * This is the receiving analogue of the `*DataCovnerter` objects.
 *
 * @param plugin The `AEffect` instance that should be passed to the callback
 *   function. During `WantsAEffect` we'll send back a copy of this, and when we
 *   get sent an `AEffect` instance (e.g. during `audioMasterIOChanged()`) we'll
 *   write the updated values to this isntance.
 * @param callback The function to call with the arguments received from the
 *   socket, either `AEffect::dispatcher()` or `audioMasterCallback()`.
 *
 * @tparam F A function with the same signature as `AEffect::dispatcher` or
 *   `audioMasterCallback`.
 *
 * @return The result of the operation. If necessary the `DataConverter` will
 *   unmarshall the payload again and write it back.
 *
 * @relates EventHandler::receive_events
 */
template <typename F>
EventResult passthrough_event(AEffect* plugin, F callback, Event& event) {
    // This buffer is used to write strings and small objects to. We'll
    // initialize the beginning with null values to both prevent it from being
    // read as some arbitrary C-style string, and to make sure that
    // `*static_cast<void**>(string_buffer.data)` will be a null pointer if the
    // plugin is supposed to write a pointer there but doesn't (such as with
    // `effEditGetRect`/`WantsVstRect`).
    std::array<char, max_string_length> string_buffer;
    std::fill(string_buffer.begin(), string_buffer.begin() + sizeof(size_t), 0);

    auto read_payload_fn = overload{
        [&](const std::nullptr_t&) -> void* { return nullptr; },
        [&](const std::string& s) -> void* {
            return const_cast<char*>(s.c_str());
        },
        [&](const ChunkData& chunk) -> void* {
            return const_cast<uint8_t*>(chunk.buffer.data());
        },
        [&](native_size_t& window_handle) -> void* {
            // This is the X11 window handle that the editor should reparent
            // itself to. We have a special wrapper around the dispatch function
            // that intercepts `effEditOpen` events and creates a Win32 window
            // and then finally embeds the X11 window Wine created into this
            // wnidow handle. Make sure to convert the window ID first to
            // `size_t` in case this is the 32-bit host.
            return reinterpret_cast<void*>(static_cast<size_t>(window_handle));
        },
        [&](const AEffect&) -> void* { return nullptr; },
        [&](DynamicVstEvents& events) -> void* {
            return &events.as_c_events();
        },
        [&](DynamicSpeakerArrangement& speaker_arrangement) -> void* {
            return &speaker_arrangement.as_c_speaker_arrangement();
        },
        [&](WantsAEffectUpdate&) -> void* {
            // The host will never actually ask for an updated `AEffect` object
            // since that should not be a thing. This is purely a meant as a
            // workaround for plugins that initialize their `AEffect` object
            // after the plugin has already finished initializing.
            return nullptr;
        },
        [&](WantsChunkBuffer&) -> void* { return string_buffer.data(); },
        [&](VstIOProperties& props) -> void* { return &props; },
        [&](VstMidiKeyName& key_name) -> void* { return &key_name; },
        [&](VstParameterProperties& props) -> void* { return &props; },
        [&](WantsVstRect&) -> void* { return string_buffer.data(); },
        [&](const WantsVstTimeInfo&) -> void* { return nullptr; },
        [&](WantsString&) -> void* { return string_buffer.data(); }};

    // Almost all events pass data through the `data` argument. There are two
    // events, `effSetParameter` and `effGetParameter` that also pass data
    // through the value argument.
    void* data = std::visit(read_payload_fn, event.payload);
    intptr_t value = event.value;
    if (event.value_payload) {
        value = reinterpret_cast<intptr_t>(
            std::visit(read_payload_fn, *event.value_payload));
    }

    const intptr_t return_value =
        callback(plugin, event.opcode, event.index, value, data, event.option);

    // Only write back data when needed, this depends on the event payload type
    auto write_payload_fn = overload{
        [&](auto) -> EventResultPayload { return nullptr; },
        [&](const AEffect& updated_plugin) -> EventResultPayload {
            // This is a bit of a special case! Instead of writing some return
            // value, we will update values on the native VST plugin's `AEffect`
            // object. This is triggered by the `audioMasterIOChanged` callback
            // from the hosted VST plugin.
            update_aeffect(*plugin, updated_plugin);

            return nullptr;
        },
        [&](DynamicSpeakerArrangement& speaker_arrangement)
            -> EventResultPayload { return speaker_arrangement; },
        [&](WantsAEffectUpdate&) -> EventResultPayload { return *plugin; },
        [&](WantsChunkBuffer&) -> EventResultPayload {
            // In this case the plugin will have written its data stored in an
            // array to which a pointer is stored in `data`, with the return
            // value from the event determines how much data the plugin has
            // written
            const uint8_t* chunk_data = *static_cast<uint8_t**>(data);
            return ChunkData{
                std::vector<uint8_t>(chunk_data, chunk_data + return_value)};
        },
        [&](WantsVstRect&) -> EventResultPayload {
            // The plugin should have written a pointer to a VstRect struct into
            // the data pointer. I haven't seen this fail yet, but since some
            // hosts will call `effEditGetRect()` before `effEditOpen()` I can
            // assume there are plugins that don't handle this correctly.
            VstRect* editor_rect = *static_cast<VstRect**>(data);
            if (!editor_rect) {
                return nullptr;
            }

            return *editor_rect;
        },
        [&](WantsVstTimeInfo&) -> EventResultPayload {
            // Not sure why the VST API has twenty different ways of
            // returning structs, but in this case the value returned from
            // the callback function is actually a pointer to a
            // `VstTimeInfo` struct! It can also be a null pointer if the
            // host doesn't support this.
            const auto time_info =
                reinterpret_cast<const VstTimeInfo*>(return_value);
            if (!time_info) {
                return nullptr;
            } else {
                return *time_info;
            }
        },
        [&](WantsString&) -> EventResultPayload {
            return std::string(static_cast<char*>(data));
        },
        [&](VstIOProperties& props) -> EventResultPayload { return props; },
        [&](VstMidiKeyName& key_name) -> EventResultPayload {
            return key_name;
        },
        [&](VstParameterProperties& props) -> EventResultPayload {
            return props;
        }};

    // As mentioned about, the `effSetSpeakerArrangement` and
    // `effGetSpeakerArrangement` events are the only two events that use the
    // value argument as a pointer to write data to. Additionally, the
    // `effGetSpeakerArrangement` expects the plugin to write its own data to
    // this value. Hence why we need to encode the response here separately.
    const EventResultPayload response_data =
        std::visit(write_payload_fn, event.payload);
    std::optional<EventResultPayload> value_response_data = std::nullopt;
    if (event.value_payload) {
        value_response_data =
            std::visit(write_payload_fn, *event.value_payload);
    }

    EventResult response{.return_value = return_value,
                         .payload = response_data,
                         .value_payload = value_response_data};

    return response;
}
