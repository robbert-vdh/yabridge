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

#include <atomic>

#include "../logging/vst2.h"
#include "../serialization/vst2.h"
#include "common.h"

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
     * Write the response back to the `data` pointer.
     */
    virtual void write(const int opcode,
                       void* data,
                       const EventResult& response) const;

    /**
     * Write the response back to the `value` pointer. This is only used during
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
 * An instance of `AdHocSocketHandler` that can handle VST2 `dispatcher()` and
 * `audioMaster()` events.
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
 *   way as every other socket defined in the `Vst2Sockets` class.
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
class EventHandler : public AdHocSocketHandler<Thread> {
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
        : AdHocSocketHandler<Thread>(io_context, endpoint, listen) {}

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
                        std::optional<std::pair<Vst2Logger&, bool>> logging,
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
        // messages from arriving out of order. `AdHocSocketHandler::send()`
        // will either use a long-living primary socket, or if that's currently
        // in use it will spawn a new socket for us.
        EventResult response = this->template send<EventResult>(
            [&](boost::asio::local::stream_protocol::socket& socket) {
                write_object(socket, event);
                return read_object<EventResult>(socket);
            });

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
     * then start a blocking loop that handles events from the primary `socket`.
     *
     * The specified function will be used to create an `EventResult` from an
     * `Event`. This is almost uses `passthrough_event()`, which converts a
     * `EventPayload` into the format used by VST2, calls either `dispatch()` or
     * `audioMaster()` depending on the context, and then serializes the result
     * back into an `EventResultPayload`.
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
    void receive_events(std::optional<std::pair<Vst2Logger&, bool>> logging,
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

        this->receive_multi(
            logging ? std::optional(std::ref(logging->first.logger))
                    : std::nullopt,
            [&](boost::asio::local::stream_protocol::socket& socket) {
                process_event(socket, true);
            },
            [&](boost::asio::local::stream_protocol::socket& socket) {
                process_event(socket, false);
            });
    }
};

/**
 * Manages all the sockets used for communicating between the plugin and the
 * Wine host when hosting a VST2 plugin.
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
class Vst2Sockets : public Sockets {
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
     * @see Vst2Sockets::connect
     */
    Vst2Sockets(boost::asio::io_context& io_context,
                const boost::filesystem::path& endpoint_base_dir,
                bool listen)
        : Sockets(endpoint_base_dir),
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

    ~Vst2Sockets() { close(); }

    void connect() override {
        host_vst_dispatch.connect();
        vst_host_callback.connect();
        host_vst_parameters.connect();
        host_vst_process_replacing.connect();
        host_vst_control.connect();
    }

    void close() override {
        // Manually close all sockets so we break out of any blocking operations
        // that may still be active
        host_vst_dispatch.close();
        vst_host_callback.close();
        host_vst_parameters.close();
        host_vst_process_replacing.close();
        host_vst_control.close();
    }

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
 *   write the updated values to this instance.
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
