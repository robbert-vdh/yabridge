// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2022 Robbert van der Helm
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
 * Manages all the sockets used for communicating between the plugin and the
 * Wine host when hosting a VST3 plugin.
 *
 * On the plugin side this class should be initialized with `listen` set to
 * `true` before launching the Wine plugin host. This will start listening on
 * the sockets, and the call to `connect()` will then accept any incoming
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
class Vst3Sockets final : public Sockets {
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
    Vst3Sockets(asio::io_context& io_context,
                const ghc::filesystem::path& endpoint_base_dir,
                bool listen)
        : Sockets(endpoint_base_dir),
          host_plugin_control_(
              io_context,
              (base_dir_ / "host_plugin_control.sock").string(),
              listen),
          plugin_host_callback_(
              io_context,
              (base_dir_ / "plugin_host_callback.sock").string(),
              listen),
          io_context_(io_context) {}

    // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall)
    ~Vst3Sockets() noexcept override { close(); }

    void connect() override {
        host_plugin_control_.connect();
        plugin_host_callback_.connect();
    }

    void close() override {
        // Manually close all sockets so we break out of any blocking operations
        // that may still be active
        host_plugin_control_.close();
        plugin_host_callback_.close();

        // This map should be empty at this point, but who knows
        std::lock_guard lock(audio_processor_sockets_mutex_);
        for (auto& [instance_id, socket] : audio_processor_sockets_) {
            socket.close();
        }
    }

    /**
     * Connect to the dedicated `IAudioProcessor` and `IComponent` handling
     * socket for a plugin object instance. This should be called on the plugin
     * side after instantiating such an object.
     *
     * @param instance_id The object instance identifier of the socket.
     */
    void add_audio_processor_and_connect(size_t instance_id) {
        std::lock_guard lock(audio_processor_sockets_mutex_);
        audio_processor_sockets_.try_emplace(
            instance_id, io_context_,
            (base_dir_ / ("host_plugin_audio_processor_" +
                          std::to_string(instance_id) + ".sock"))
                .string(),
            false);

        audio_processor_sockets_.at(instance_id).connect();
    }

    /**
     * Create and listen on a dedicated `IAudioProcessor` and `IComponent`
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
     *   `Vst3AudioProcessorRequest` variant and then returns `T::Response`.
     *
     * @tparam F A function type in the form of `T::Response(T)` for every `T`
     *   in `Vst3AudioProcessorRequest::Payload`.
     */
    template <typename F>
    void add_audio_processor_and_listen(
        size_t instance_id,
        std::promise<void>& socket_listening_latch,
        F&& callback) {
        {
            std::lock_guard lock(audio_processor_sockets_mutex_);
            audio_processor_sockets_.try_emplace(
                instance_id, io_context_,
                (base_dir_ / ("host_plugin_audio_processor_" +
                              std::to_string(instance_id) + ".sock"))
                    .string(),
                true);
        }

        socket_listening_latch.set_value();
        audio_processor_sockets_.at(instance_id).connect();

        // This `true` indicates that we want to reuse our serialization and
        // receiving buffers for all calls. This slightly reduces the amount of
        // allocations in the audio processing loop.
        audio_processor_sockets_.at(instance_id)
            .template receive_messages<true>(std::nullopt,
                                             std::forward<F>(callback));
    }

    /**
     * If `instance_id` is in `audio_processor_sockets_`, then close its socket
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
        std::lock_guard lock(audio_processor_sockets_mutex_);
        if (audio_processor_sockets_.contains(instance_id)) {
            audio_processor_sockets_.at(instance_id).close();
            audio_processor_sockets_.erase(instance_id);

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
     * @tparam T Some object in the `Vst3AudioProcessorRequest` variant. All of
     *   these objects need to have an `instance_id` field.
     */
    template <typename T>
    typename T::Response send_audio_processor_message(
        const T& object,
        std::optional<std::pair<Vst3Logger&, bool>> logging) {
        typename T::Response response_object;
        return receive_audio_processor_message_into(
            object, response_object, object.instance_id, logging);
    }

    /**
     * Overload for use with `MessageReference<T>`, since we cannot
     * directly get the instance ID there.
     */
    template <typename T>
    typename T::Response send_audio_processor_message(
        const MessageReference<T>& object_ref,
        std::optional<std::pair<Vst3Logger&, bool>> logging) {
        typename T::Response response_object;
        return receive_audio_processor_message_into(
            object_ref, response_object, object_ref.get().instance_id, logging);
    }

    /**
     * Alternative to `send_audio_processor_message()` for use with
     * `MessageReference<T>`, where we also want deserialize into an existing
     * object to prevent allocations. Used during audio processing.
     *
     * TODO: Think of a better name for this
     */
    template <typename T>
    typename T::Response& receive_audio_processor_message_into(
        const MessageReference<T>& request_ref,
        typename T::Response& response_ref,
        std::optional<std::pair<Vst3Logger&, bool>> logging) {
        return receive_audio_processor_message_into(
            request_ref, response_ref, request_ref.get().instance_id, logging);
    }

    /**
     * For sending messages from the host to the plugin. After we have a better
     * idea of what our communication model looks like we'll probably want to
     * provide an abstraction similar to `Vst2EventHandler`. For optimization
     * reasons calls to `IAudioProcessor` or `IComponent` are handled using the
     * dedicated sockets in `audio_processor_sockets`.
     *
     * This will be listened on by the Wine plugin host when it calls
     * `receive_multi()`.
     */
    TypedMessageHandler<Thread, Vst3Logger, Vst3ControlRequest>
        host_plugin_control_;

    /**
     * For sending callbacks from the plugin back to the host. After we have a
     * better idea of what our communication model looks like we'll probably
     * want to provide an abstraction similar to `Vst2EventHandler`.
     */
    TypedMessageHandler<Thread, Vst3Logger, Vst3CallbackRequest>
        plugin_host_callback_;

   private:
    /**
     * The actual implementation for `send_audio_processor_message` and
     * `receive_audio_processor_message_into`. Here we keep a thread local
     * static variable for our buffers sending.
     */
    template <typename T>
    typename T::Response& receive_audio_processor_message_into(
        const T& object,
        typename T::Response& response_object,
        size_t instance_id,
        std::optional<std::pair<Vst3Logger&, bool>> logging) {
        thread_local SerializationBuffer<2048> audio_processor_buffer{};

        return audio_processor_sockets_.at(instance_id)
            .receive_into(object, response_object, logging,
                          audio_processor_buffer);
    }

    asio::io_context& io_context_;

    /**
     * Every `IAudioProcessor` or `IComponent` instance (which likely implements
     * both of those) will get a dedicated socket. These functions are always
     * called in a hot loop, so there should not be any waiting or additional
     * thread or socket creation happening there.
     */
    std::unordered_map<
        size_t,
        TypedMessageHandler<Thread, Vst3Logger, Vst3AudioProcessorRequest>>
        audio_processor_sockets_;
    std::mutex audio_processor_sockets_mutex_;
};
