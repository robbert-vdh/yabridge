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

#include <future>
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
     * @param buffer The serialization and receiving buffer to reuse. This is
     *   optional, but it's useful for minimizing allocations in the audio
     *   processing loop.
     *
     * @relates Vst3MessageHandler::receive_messages
     */
    template <typename T>
    typename T::Response send_message(
        const T& object,
        std::optional<std::pair<Vst3Logger&, bool>> logging,
        std::vector<uint8_t>& buffer) {
        typename T::Response response_object;
        receive_into(object, response_object, logging, buffer);

        return response_object;
    }

    /**
     * The same as the above, but with a small default buffer.
     *
     * @overload
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
     * @param response_object The object to deserialize into.
     *
     * @overload Vst3MessageHandler::send_message
     */
    template <typename T>
    typename T::Response& receive_into(
        const T& object,
        typename T::Response& response_object,
        std::optional<std::pair<Vst3Logger&, bool>> logging,
        std::vector<uint8_t>& buffer) {
        using TResponse = typename T::Response;

        // Since a lot of messages just return a `tresult`, we can't filter out
        // responses based on the response message type. Instead, we'll just
        // only print the responses when the request was not filtered out.
        bool should_log_response = false;
        if (logging) {
            auto [logger, is_host_vst] = *logging;
            should_log_response = logger.log_request(is_host_vst, object);
        }

        // A socket only handles a single request at a time as to prevent
        // messages from arriving out of order. `AdHocSocketHandler::send()`
        // will either use a long-living primary socket, or if that's currently
        // in use it will spawn a new socket for us.
        this->template send<std::monostate>(
            [&](boost::asio::local::stream_protocol::socket& socket) {
                write_object(socket, Request(object), buffer);
                read_object<TResponse>(socket, response_object, buffer);
                // FIXME: We have to return something here, and ML was not yet
                //        invented when they came up with C++ so void is not
                //        valid here
                return std::monostate{};
            });

        if (should_log_response) {
            auto [logger, is_host_vst] = *logging;
            logger.log_response(!is_host_vst, response_object);
        }

        return response_object;
    }

    /**
     * The same function as above, but with a small default buffer.
     *
     * @overload
     */
    template <typename T>
    typename T::Response& receive_into(
        const T& object,
        typename T::Response& response_object,
        std::optional<std::pair<Vst3Logger&, bool>> logging) {
        std::vector<uint8_t> buffer(64);
        return receive_into(object, response_object, std::move(logging),
                            buffer);
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
     * @tparam F A function type in the form of `T::Response(T)` for every `T`
     *   in `Request`. This way we can directly deserialize into a `T::Response`
     *   on the side that called `receive_into(T, T::Response&)`.
     * @tparam persistent_buffers If enabled, we'll reuse the buffers used for
     *   sending and receiving serialized data as well as the objects we're
     *   receiving into. This avoids allocations in the audio processing loop
     *   (after the first allocation of course). This is mostly relevant for the
     *   `YaProcessData` object stored inside of `YaAudioProcessor::Process`.
     *   These buffers are thread local and will also never shrink, but that
     *   should not be an issue with the `IAudioProcessor` and `IComponent`
     *   functions.  Saving and loading state is handled on the main sockets,
     *   which don't use these persistent buffers.
     *
     * @relates Vst3MessageHandler::send_event
     */
    template <bool persistent_buffers = false, typename F>
    void receive_messages(std::optional<std::pair<Vst3Logger&, bool>> logging,
                          F callback) {
        thread_local std::vector<uint8_t> persistent_buffer{};
        // This is an `std::variant<>`, so this will actually persistently store
        // a copy of all possible requests even if we're only interested in the
        // process data, since that's the only object where allocations can
        // happen. The other objects we're storing here are very small, so the
        // extra wasted memory shouldn't matter much.
        // FIXME: This of course doesn't work. The variant will have been
        //        reconstructed when the next process cycle hits, so we don't
        //        gain anything from this.
        thread_local std::optional<Request> persistent_object =
            persistent_buffers ? std::make_optional<Request>() : std::nullopt;

        // Reading, processing, and writing back the response for the requests
        // we receive works in the same way regardless of which socket we're
        // using
        const auto process_message =
            [&](boost::asio::local::stream_protocol::socket& socket) {
                auto request =
                    persistent_buffers
                        ? read_object<Request>(socket, *persistent_object,
                                               persistent_buffer)
                        : read_object<Request>(socket);

                // See the comment in `receive_into()` for more information
                bool should_log_response = false;
                if (logging) {
                    should_log_response = std::visit(
                        [&](const auto& object) {
                            auto [logger, is_host_vst] = *logging;
                            return logger.log_request(is_host_vst, object);
                        },
                        // In the case of `AudioProcessorRequest`, we need to
                        // actually fetch the variant field since our object
                        // also contains a persistent object to store process
                        // data into so we can prevent allocations during audio
                        // processing
                        get_request_variant(request));
                }

                // We do the visiting here using a templated lambda. This way we
                // always know for sure that the function returns the correct
                // type, and we can scrap a lot of boilerplate elsewhere.
                std::visit(
                    [&]<typename T>(T object) {
                        typename T::Response response = callback(object);

                        if (should_log_response) {
                            auto [logger, is_host_vst] = *logging;
                            logger.log_response(!is_host_vst, response);
                        }

                        if constexpr (persistent_buffers) {
                            write_object(socket, response, persistent_buffer);
                        } else {
                            write_object(socket, response);
                        }
                    },
                    // See above
                    get_request_variant(request));
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
 * We'll have a host -> plugin connection for sending control messages (which is
 * just a made up term to more easily differentiate between the two directions),
 * and a plugin -> host connection to allow the plugin to make callbacks. Both
 * of these connections are capable of spawning additional sockets and threads
 * as needed.
 *
 * For audio processing (or anything that implement `IAudioProcessor` or
 * `IComonent`) we'll use dedicated sockets per instance, since we don't want to
 * do anything that could increase latency there.
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
                            listen),
          io_context(io_context) {}

    // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall)
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

        // This map should be empty at this point, but who knows
        std::lock_guard lock(audio_processor_sockets_mutex);
        for (auto& [instance_id, socket] : audio_processor_sockets) {
            socket.close();
        }
    }

    /**
     * Connect to the dedicated `IAudioProcessor` and `IConnect` handling socket
     * for a plugin object instance. This should be called on the plugin side
     * after instantiating such an object.
     *
     * @param instance_id The object instance identifier of the socket.
     */
    void add_audio_processor_and_connect(size_t instance_id) {
        std::lock_guard lock(audio_processor_sockets_mutex);
        audio_processor_sockets.try_emplace(
            instance_id, io_context,
            (base_dir / ("host_vst_audio_processor_" +
                         std::to_string(instance_id) + ".sock"))
                .string(),
            false);
        audio_processor_buffers.try_emplace(instance_id);

        audio_processor_sockets.at(instance_id).connect();
    }

    /**
     * Create and listen on a dedicated `IAudioProcessor` and `IConnect`
     * handling socket for a plugin object instance. The calling thread will
     * block until the socket has been closed. This should be called from the
     * Wine plugin host side after instantiating such an object.
     *
     * @param instance_id The object instance identifier of the socket.
     * @param socket_listening_latch A promise we'll set a value for once the
     *   socket is being listened on so we can wait for it. Otherwise it can be
     *   that the native plugin already tries to connect to the socket before
     *   Wine plugin host is even listening on it.
     * @param cb An overloaded function that can take every type `T` in the
     *   `AudioProcessorRequest` variant and then returns `T::Response`.
     */
    template <typename F>
    void add_audio_processor_and_listen(
        size_t instance_id,
        std::promise<void>& socket_listening_latch,
        F cb) {
        {
            std::lock_guard lock(audio_processor_sockets_mutex);
            audio_processor_sockets.try_emplace(
                instance_id, io_context,
                (base_dir / ("host_vst_audio_processor_" +
                             std::to_string(instance_id) + ".sock"))
                    .string(),
                true);
            audio_processor_buffers.try_emplace(instance_id);
        }

        socket_listening_latch.set_value();
        audio_processor_sockets.at(instance_id).connect();

        // This `true` indicates that we want to reuse our serialization and
        // receiving buffers for all calls. This slightly reduces the amount of
        // allocations in the audio processing loop.
        audio_processor_sockets.at(instance_id)
            .template receive_messages<true>(std::nullopt, cb);
    }

    /**
     * If `instance_id` is in `audio_processor_sockets`, then close its socket
     * and remove it from the map. This is called from the destructor of
     * `Vst3PluginProxyImpl` on the plugin side and when handling
     * `Vst3PluginProxy::Destruct` on the Wine plugin host side.
     *
     * @param instance_id The object instance identifier of the socket.
     *
     * @return Whether the socket was closed and removed. Returns false if it
     *   wasn't in the map.
     */
    bool remove_audio_processor(size_t instance_id) {
        std::lock_guard lock(audio_processor_sockets_mutex);
        if (audio_processor_sockets.contains(instance_id)) {
            audio_processor_sockets.at(instance_id).close();
            audio_processor_sockets.erase(instance_id);
            audio_processor_buffers.erase(instance_id);

            return true;
        } else {
            return false;
        }
    }

    /**
     * Send a message from the native plugin to the Wine plugin host to handle
     * an `IAudioProcessor` or `IComponent` call. Since those functions are
     * called from a hot loop we want every instance to have a dedicated socket
     * and thread for handling those. These calls also always reuse buffers to
     * minimize allocations.
     *
     * @tparam T Some object in the `AudioProcessorRequest` variant.
     */
    template <typename T>
    typename T::Response send_audio_processor_message(
        const T& object,
        std::optional<std::pair<Vst3Logger&, bool>> logging) {
        return audio_processor_sockets.at(object.instance_id)
            .send_message(object, logging,
                          audio_processor_buffers.at(object.instance_id));
    }

    /**
     * Overload for use with `MessageReference<T>`, since we cannot
     * directly get the instance ID there.
     */
    template <typename T>
    typename T::Response send_audio_processor_message(
        const MessageReference<T>& object_ref,
        std::optional<std::pair<Vst3Logger&, bool>> logging) {
        return audio_processor_sockets.at(object_ref.get().instance_id)
            .send_message(
                object_ref, logging,
                audio_processor_buffers.at(object_ref.get().instance_id));
    }

    /**
     * For sending messages from the host to the plugin. After we have a better
     * idea of what our communication model looks like we'll probably want to
     * provide an abstraction similar to `EventHandler`. For optimization
     * reasons calls to `IAudioProcessor` or `IComponent` are handled using the
     * dedicated sockets in `audio_processor_sockets`.
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

   private:
    boost::asio::io_context& io_context;

    /**
     * Every `IAudioProcessor` or `IComponent` instance (which likely implements
     * both of those) will get a dedicated socket. These functions are always
     * called in a hot loop, so there should not be any waiting or additional
     * thread or socket creation happening there.
     *
     * THe last `false` template arguments means that we'll disable all ad-hoc
     * socket and thread spawning behaviour. Otherwise every plugin instance
     * would have one dedicated thread for handling function calls to these
     * interfaces, and then another dedicated thread just idling around.
     */
    std::map<size_t, Vst3MessageHandler<Thread, AudioProcessorRequest>>
        audio_processor_sockets;
    /**
     * Binary buffers used for serializing objects and receiving messages into
     * during `send_audio_processor_message()`. This is used to minimize the
     * amount of allocations in the audio processing loop.
     */
    std::map<size_t, std::vector<uint8_t>> audio_processor_buffers;
    std::mutex audio_processor_sockets_mutex;
};
