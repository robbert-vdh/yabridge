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

#include "../logging/vst3.h"
#include "../serialization/vst3.h"
#include "common.h"

/**
 * An instance of `AdHocSocketHandler` that encapsulates the simple
 * communication model we use for sending requests and receiving responses. A
 * request of type `T`, where `T` is in `{Control,Callback}Request`, should be
 * answered with an object of type `T::Response`.
 *
 * See the docstrings on `EventHandler` and `AdHocSocketHandler` for more
 * information on how this works internally and why it works the way it does.
 *
 * @note The name of this class is not to be confused with VST3's `IMessage` as
 *   this is very much just general purpose messaging between yabridge's two
 *   components. Of course, this will handle `IMessage` function calls as well.
 *
 * @tparam Thread The thread implementation to use. On the Linux side this
 *   should be `std::jthread` and on the Wine side this should be `Win32Thread`.
 * @tparam Request Either `ControlRequest` or `CallbackRequest`.
 */
template <typename Thread, typename Request>
class Vst3MessageHandler : public AdHocSocketHandler<Thread> {
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
    Vst3MessageHandler(boost::asio::io_context& io_context,
                       boost::asio::local::stream_protocol::endpoint endpoint,
                       bool listen)
        : AdHocSocketHandler<Thread>(io_context, endpoint, listen) {}

    /**
     * Serialize and send an event over a socket and return the appropriate
     * response.
     *
     * As described above, if this function is currently being called from
     * another thread, then this will create a new socket connection and send
     * the event there instead.
     *
     * @param object The request object to send. Often a marker struct to ask
     *   for a specific object to be returned.
     * @param logging A pair containing a logger instance and whether or not
     *   this is for sending host -> plugin control messages. If set to false,
     *   then this indicates that this `Vst3MessageHandler` is handling plugin
     *   -> host callbacks isntead. Optional since it only has to be set on the
     *   plugin's side.
     *
     * TODO: Is it feasible to move `logging` to the constructor instead?
     *
     * @relates Vst3MessageHandler::receive_messages
     */
    template <typename T>
    typename T::Response send_message(
        const T& object,
        std::optional<std::pair<Vst3Logger&, bool>> logging) {
        typename T::Response response_object;
        receive_into(object, response_object, logging);

        return response_object;
    }

    /**
     * `Vst3MessageHandler::send_message()`, but deserializing the response into
     * an existing object.
     *
     * TODO: We might also need overloads that reuse buffers
     *
     * @param response_object The object to deserialize into.
     *
     * @overload Vst3MessageHandler::send_message
     */
    template <typename T>
    typename T::Response& receive_into(
        const T& object,
        typename T::Response& response_object,
        std::optional<std::pair<Vst3Logger&, bool>> logging) {
        using TResponse = typename T::Response;

        if (logging) {
            auto [logger, is_host_vst] = *logging;
            logger.log_request(is_host_vst, object);
        }

        // A socket only handles a single request at a time as to prevent
        // messages from arriving out of order. `AdHocSocketHandler::send()`
        // will either use a long-living primary socket, or if that's currently
        // in use it will spawn a new socket for us.
        this->template send<std::monostate>(
            [&](boost::asio::local::stream_protocol::socket& socket) {
                write_object(socket, Request(object));
                read_object<TResponse>(socket, response_object);
                // FIXME: We have to return something here, and ML was not yet
                //        invented when they came up with C++ so void is not
                //        valid here
                return std::monostate{};
            });

        if (logging) {
            auto [logger, is_host_vst] = *logging;
            logger.log_response(!is_host_vst, response_object);
        }

        return response_object;
    }

    /**
     * Spawn a new thread to listen for extra connections to `endpoint`, and
     * then start a blocking loop that handles messages from the primary
     * `socket`.
     *
     * The specified function receives a `Request` variant object containing an
     * object of type `T`, and it should then return the corresponding
     * `T::Response`.
     *
     * @param logging A pair containing a logger instance and whether or not
     *   this is for sending host -> plugin control messages. If set to false,
     *   then this indicates that this `Vst3MessageHandler` is handling plugin
     *   -> host callbacks isntead. Optional since it only has to be set on the
     *   plugin's side.
     * @param callback The function used to generate a response out of the
     *   request.  See the definition of `F` for more information.
     *
     * @tparam F A function type in the form of `T::Response(Request(T))`. C++
     *   doesn't have syntax for this, but the function receives a `Request`
     *   variant containing a `T`, and the function should return a `T::Reponse`
     *   object. This way we can directly deserialize into a `T::Reponse` on the
     *   side that called `send_object(T)`.
     *
     * @relates Vst3MessageHandler::send_event
     */
    template <typename F>
    void receive_messages(std::optional<std::pair<Vst3Logger&, bool>> logging,
                          F callback) {
        // Reading, processing, and writing back the response for the requests
        // we receive works in the same way regardless of which socket we're
        // using
        const auto process_message =
            [&](boost::asio::local::stream_protocol::socket& socket) {
                auto request = read_object<Request>(socket);
                if (logging) {
                    std::visit(
                        [&](const auto& object) {
                            auto [logger, is_host_vst] = *logging;
                            logger.log_request(is_host_vst, object);
                        },
                        request);
                }

                const auto response = callback(request);
                if (logging) {
                    auto [logger, is_host_vst] = *logging;
                    logger.log_response(!is_host_vst, response);
                }

                write_object(socket, response);
            };

        this->receive_multi(logging
                                ? std::optional(std::ref(logging->first.logger))
                                : std::nullopt,
                            process_message);
    }
};

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
        : Sockets(endpoint_base_dir),
          host_vst_control(io_context,
                           (base_dir / "host_vst_control.sock").string(),
                           listen),
          vst_host_callback(io_context,
                            (base_dir / "vst_host_callback.sock").string(),
                            listen) {}

    ~Vst3Sockets() { close(); }

    void connect() override {
        host_vst_control.connect();
        vst_host_callback.connect();
    }

    void close() override {
        // Manually close all sockets so we break out of any blocking operations
        // that may still be active
        host_vst_control.close();
        vst_host_callback.close();
    }

    // TODO: Since audio processing may be done completely in parallel we might
    //       want to have a dedicated socket per processor/controller pair. For
    //       this we would need to figure out how to associate a plugin instance
    //       with a socket.

    /**
     * For sending messages from the host to the plugin. After we have a better
     * idea of what our communication model looks like we'll probably want to
     * provide an abstraction similar to `EventHandler`.
     *
     * This will be listened on by the Wine plugin host when it calls
     * `receive_multi()`.
     */
    Vst3MessageHandler<Thread, ControlRequest> host_vst_control;

    /**
     * For sending callbacks from the plugin back to the host. After we have a
     * better idea of what our communication model looks like we'll probably
     * want to provide an abstraction similar to `EventHandler`.
     */
    Vst3MessageHandler<Thread, CallbackRequest> vst_host_callback;
};
