// yabridge: a Wine VST bridge
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

#include <atomic>

#include "../logging/vst2.h"
#include "../serialization/vst2.h"
#include "../utils.h"
#include "common.h"

/**
 * Encodes the base behavior for reading from and writing to the `data` argument
 * for event dispatch functions. This provides base functionality for these
 * kinds of events. The `dispatch()` function will require some more specific
 * structs.
 */
class DefaultDataConverter {
   public:
    virtual ~DefaultDataConverter() noexcept;

    /**
     * Read data from the `data` void pointer into a an `Vst2Event::Payload`
     * value that can be serialized and conveys the meaning of the event.
     */
    virtual Vst2Event::Payload read_data(const int opcode,
                                         const int index,
                                         const intptr_t value,
                                         const void* data) const;

    /**
     * Read data from the `value` pointer into a an `Vst2Event::Payload` value
     * that can be serialized and conveys the meaning of the event. This is only
     * used for the `effSetSpeakerArrangement` and `effGetSpeakerArrangement`
     * events.
     */
    virtual std::optional<Vst2Event::Payload> read_value(
        const int opcode,
        const intptr_t value) const;

    /**
     * Write the response back to the `data` pointer.
     */
    virtual void write_data(const int opcode,
                            void* data,
                            const Vst2EventResult& response) const;

    /**
     * Write the response back to the `value` pointer. This is only used during
     * the `effGetSpeakerArrangement` event.
     */
    virtual void write_value(const int opcode,
                             intptr_t value,
                             const Vst2EventResult& response) const;

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

    /**
     * Send an event over the socket. The default implementation will just send
     * the event over the socket, and then wait for the response to be sent
     * back. This can be overridden to use `MutualRecursionHelper::fork()` for
     * specific opcodes to allow mutually recursive calling sequences.
     */
    virtual Vst2EventResult send_event(
        asio::local::stream_protocol::socket& socket,
        const Vst2Event& event,
        SerializationBufferBase& buffer) const;
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
 * The `Vst2EventHandler::send_event()` method is used to send events. If the
 * socket is currently being written to, we'll first create a new socket
 * connection as described above. Similarly, the
 * `Vst2EventHandler::receive_events()` method first sets up asynchronous
 * listeners for the socket endpoint, and then block and handle events until the
 * main socket is closed.
 *
 * @tparam Thread The thread implementation to use. On the Linux side this
 *   should be `std::jthread` and on the Wine side this should be `Win32Thread`.
 */
template <typename Thread>
class Vst2EventHandler : public AdHocSocketHandler<Thread> {
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
    Vst2EventHandler(asio::io_context& io_context,
                     asio::local::stream_protocol::endpoint endpoint,
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
     *   `Vst2Event::Payload` for more information. The `DefaultDataConverter`
     *   defined above handles the basic behavior that's sufficient for host
     *   callbacks.
     * @param logging A pair containing a logger instance and whether or not
     *   this is for sending `dispatch()` events or host callbacks. Optional
     *   since it doesn't have to be done on both sides.
     *
     * @relates Vst2EventHandler::receive_events
     * @relates passthrough_event
     */
    template <std::derived_from<DefaultDataConverter> Converter>
    intptr_t send_event(Converter& data_converter,
                        std::optional<std::pair<Vst2Logger&, bool>> logging,
                        int opcode,
                        int index,
                        intptr_t value,
                        void* data,
                        float option) {
        // Encode the right payload types for this event. Check the
        // documentation for `Vst2Event::Payload` for more information. These
        // types are converted to C-style data structures in
        // `passthrough_event()` so they can be passed to a plugin or callback
        // function.
        const Vst2Event::Payload payload =
            data_converter.read_data(opcode, index, value, data);
        const std::optional<Vst2Event::Payload> value_payload =
            data_converter.read_value(opcode, value);

        if (logging) {
            auto [logger, is_dispatch] = *logging;
            logger.log_event(is_dispatch, opcode, index, value, payload, option,
                             value_payload);
        }

        const Vst2Event event{.opcode = opcode,
                              .index = index,
                              .value = value,
                              .option = option,
                              .payload = std::move(payload),
                              .value_payload = std::move(value_payload)};

        // A socket only handles a single request at a time as to prevent
        // messages from arriving out of order. `AdHocSocketHandler::send()`
        // will either use a long-living primary socket, or if that's currently
        // in use it will spawn a new socket for us. We'll then use
        // `DefaultDataConverter::send_event()` to actually write and read data
        // from the socket, so we can override this for specific function calls
        // that potentially need to have their responses handled on the same
        // calling thread (i.e. mutual recursion).
        const Vst2EventResult response =
            this->send([&](asio::local::stream_protocol::socket& socket) {
                return data_converter.send_event(socket, event,
                                                 serialization_buffer());
            });

        if (logging) {
            auto [logger, is_dispatch] = *logging;
            logger.log_event_response(is_dispatch, opcode,
                                      response.return_value, response.payload,
                                      response.value_payload);
        }

        data_converter.write_data(opcode, data, response);
        data_converter.write_value(opcode, value, response);

        return data_converter.return_value(opcode, response.return_value);
    }

    /**
     * Spawn a new thread to listen for extra connections to `endpoint_`, and
     * then start a blocking loop that handles events from the primary
     * `socket_`.
     *
     * The specified function will be used to create an `Vst2EventResult` from
     * an `Vst2Event`. This is almost always uses `passthrough_event()`, which
     * converts a `Vst2Event::Payload` into the format used by VST2, calls
     * either `dispatch()` or `audioMaster()` depending on the context, and then
     * serializes the result back into an `Vst2EventResult::Payload`.
     *
     * @param logging A pair containing a logger instance and whether or not
     *   this is for sending `dispatch()` events or host callbacks. Optional
     *   since it doesn't have to be done on both sides.
     * @param callback The function used to generate a response out of an event.
     *   See the definition of `F` for more information.
     *
     * @relates Vst2EventHandler::send_event
     * @relates passthrough_event
     */
    template <invocable_returning<Vst2EventResult, Vst2Event&, bool> F>
    void receive_events(std::optional<std::pair<Vst2Logger&, bool>> logging,
                        F&& callback) {
        // Reading, processing, and writing back event data from the sockets
        // works in the same way regardless of which socket we're using
        const auto process_event =
            [&](asio::local::stream_protocol::socket& socket,
                bool on_main_thread) {
                SerializationBufferBase& buffer = serialization_buffer();

                auto event = read_object<Vst2Event>(socket, buffer);
                if (logging) {
                    auto [logger, is_dispatch] = *logging;
                    logger.log_event(is_dispatch, event.opcode, event.index,
                                     event.value, event.payload, event.option,
                                     event.value_payload);
                }

                Vst2EventResult response = callback(event, on_main_thread);
                if (logging) {
                    auto [logger, is_dispatch] = *logging;
                    logger.log_event_response(
                        is_dispatch, event.opcode, response.return_value,
                        response.payload, response.value_payload);
                }

                write_object(socket, response, buffer);
            };

        this->receive_multi(
            logging ? std::optional(std::ref(logging->first.logger_))
                    : std::nullopt,
            [&](asio::local::stream_protocol::socket& socket) {
                process_event(socket, true);
            },
            [&](asio::local::stream_protocol::socket& socket) {
                process_event(socket, false);
            });
    }

   private:
    /**
     * Unlike our VST3 implementation, in the VST2 implementation there's no
     * separation between potentially real time critical events that will be
     * called on the audio thread an all other events. This is absolutely fine,
     * except for sending and receiving MIDI events. Those objects can get
     * rather large, and because we want to avoid allocations on the audio
     * thread at all cost we'll just predefine a large buffer for every thread.
     */
    SerializationBufferBase& serialization_buffer() {
        // This object also contains a `llvm::SmallVector` that has
        // capacity for a large-ish number of events so we don't have to
        // allocate under normal circumstances.
        constexpr size_t initial_events_size = sizeof(DynamicVstEvents);
        thread_local SerializationBuffer<initial_events_size> buffer{};

        // This buffer is already pretty large, but it can still grow immensely
        // when sending and receiving preset data. In such cases we do want to
        // reallocate the buffer on the next event to free up memory again. This
        // won't happen during audio processing.
        if (buffer.size() > initial_events_size * 2) {
            // NOTE: There's no `.shrink_to_fit()` implementation here, so we'll
            //       just YOLO it and reinitialize the vector since we don't
            //       need the old data
            buffer = SerializationBuffer<initial_events_size>{};
            // buffer.resize(initial_events_size);
            // buffer.shrink_to_fit();
        }

        return buffer;
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
class Vst2Sockets final : public Sockets {
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
    Vst2Sockets(asio::io_context& io_context,
                const ghc::filesystem::path& endpoint_base_dir,
                bool listen)
        : Sockets(endpoint_base_dir),
          host_vst_dispatch_(io_context,
                             (base_dir_ / "host_vst_dispatch.sock").string(),
                             listen),
          vst_host_callback_(io_context,
                             (base_dir_ / "vst_host_callback_.sock").string(),
                             listen),
          host_vst_parameters_(
              io_context,
              (base_dir_ / "host_vst_parameters.sock").string(),
              listen),
          host_vst_process_replacing_(
              io_context,
              (base_dir_ / "host_vst_process_replacing.sock").string(),
              listen),
          host_vst_control_(io_context,
                            (base_dir_ / "host_vst_control_.sock").string(),
                            listen) {}

    ~Vst2Sockets() noexcept override { close(); }

    void connect() override {
        host_vst_dispatch_.connect();
        vst_host_callback_.connect();
        host_vst_parameters_.connect();
        host_vst_process_replacing_.connect();
        host_vst_control_.connect();
    }

    void close() override {
        // Manually close all sockets so we break out of any blocking operations
        // that may still be active
        host_vst_dispatch_.close();
        vst_host_callback_.close();
        host_vst_parameters_.close();
        host_vst_process_replacing_.close();
        host_vst_control_.close();
    }

    // The naming convention for these sockets is `<from>_<to>_<event>`. For
    // instance the socket named `host_vst_dispatch` forwards
    // `AEffect.dispatch()` calls from the native VST host to the Windows VST
    // plugin (through the Wine VST host).

    /**
     * The socket that forwards all `dispatcher()` calls from the VST host to
     * the plugin.
     */
    Vst2EventHandler<Thread> host_vst_dispatch_;
    /**
     * The socket that forwards all `audioMaster()` calls from the Windows VST
     * plugin to the host.
     */
    Vst2EventHandler<Thread> vst_host_callback_;
    /**
     * Used for both `getParameter` and `setParameter` since they mostly
     * overlap.
     */
    SocketHandler host_vst_parameters_;
    /**
     * Used for processing audio usign the `process()`, `processReplacing()` and
     * `processDoubleReplacing()` functions.
     */
    SocketHandler host_vst_process_replacing_;
    /**
     * A control socket that sends data that is not suitable for the other
     * sockets. At the moment this is only used to, on startup, send the Windows
     * VST plugin's `AEffect` object to the native VST plugin, and to then send
     * the configuration (from `config_`) back to the Wine host.
     */
    SocketHandler host_vst_control_;
};

/**
 * Unmarshall an `Vst2Event::Payload` back to the representation used by VST2,
 * pass that value to a callback function (either `AEffect::dispatcher()` for
 * host -> plugin events or `audioMaster()` for plugin -> host events), and then
 * serialize the results back into an `Vst2EventResult`.
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
 * @return The result of the operation. If necessary the `DataConverter` will
 *   unmarshall the payload again and write it back.
 *
 * @relates Vst2EventHandler::receive_events
 *
 * TODO: Maybe at some point rework this to use `T::Raw` and `T::Response`
 *       associated types similar to how we handle things on the VST3 side to ad
 *       least make it slightly more type safe.
 */
template <
    invocable_returning<intptr_t, AEffect*, int, int, intptr_t, void*, float> F>
Vst2EventResult passthrough_event(AEffect* plugin,
                                  F&& callback,
                                  Vst2Event& event) {
    // This buffer is used to write strings and small objects to. We'll
    // initialize the beginning with null values to both prevent it from being
    // read as some arbitrary C-style string, and to make sure that
    // `*static_cast<void**>(string_buffer.data)` will be a null pointer if the
    // plugin is supposed to write a pointer there but doesn't (such as with
    // `effEditGetRect`/`WantsVstRect`).
    std::array<char, max_string_length> string_buffer;
    std::fill(string_buffer.begin(), string_buffer.begin() + sizeof(size_t), 0);

    auto read_payload_fn = overload{
        [](const std::nullptr_t&) -> void* { return nullptr; },
        [](const std::string& s) -> void* {
            return const_cast<char*>(s.c_str());
        },
        [](const ChunkData& chunk) -> void* {
            return const_cast<uint8_t*>(chunk.buffer.data());
        },
        [](const native_size_t& window_handle) -> void* {
            // This is the X11 window handle that the editor should reparent
            // itself to. We have a special wrapper around the dispatch function
            // that intercepts `effEditOpen` events and creates a Win32 window
            // and then finally embeds the X11 window Wine created into this
            // wnidow handle. Make sure to convert the window ID first to
            // `size_t` in case this is the 32-bit host.
            return reinterpret_cast<void*>(static_cast<size_t>(window_handle));
        },
        [](const AEffect&) -> void* {
            // This is used as a magic payload value to send `AEffect` struct
            // updates to the native plugin from the Wine plugin host
            return nullptr;
        },
        [](DynamicVstEvents& events) -> void* { return &events.as_c_events(); },
        [](DynamicSpeakerArrangement& speaker_arrangement) -> void* {
            return &speaker_arrangement.as_c_speaker_arrangement();
        },
        [](const WantsAEffectUpdate&) -> void* {
            // The host will never actually ask for an updated `AEffect` object
            // since that should not be a thing. This is purely a meant as a
            // workaround for plugins that initialize their `AEffect` object
            // after the plugin has already finished initializing.
            return nullptr;
        },
        [](const WantsAudioShmBufferConfig&) -> void* {
            // This is another magic value. We'll create the shared memory
            // object after the plugin's dispatch function has returned and then
            // return the configuration to the native plugin, in
            // `Vst2Bridge::run`.
            return nullptr;
        },
        [&](const WantsChunkBuffer&) -> void* { return string_buffer.data(); },
        [](VstIOProperties& props) -> void* { return &props; },
        [](VstMidiKeyName& key_name) -> void* { return &key_name; },
        [](VstParameterProperties& props) -> void* { return &props; },
        [&](const WantsVstRect&) -> void* { return string_buffer.data(); },
        [](const WantsVstTimeInfo&) -> void* { return nullptr; },
        [&](const WantsString&) -> void* { return string_buffer.data(); }};

    // Almost all events pass data through the `data` argument. There are two
    // events, `effSetSpeakerArrangement()` and `effGetSpeakerArrangement()`
    // that also use the value argument in the same way.
    void* data = std::visit(read_payload_fn, event.payload);
    intptr_t value = event.value;
    if (event.value_payload) {
        value = reinterpret_cast<intptr_t>(
            std::visit(read_payload_fn, *event.value_payload));
    }

    // The other arguments are simple value types that we can pass to the plugin
    // directly
    const intptr_t return_value =
        callback(plugin, event.opcode, event.index, value, data, event.option);

    // For some payload types we need to write back a value to the data pointer
    auto write_payload_fn = overload{
        [](const auto&) -> Vst2EventResult::Payload { return nullptr; },
        [&](const AEffect& updated_plugin) -> Vst2EventResult::Payload {
            // This is a bit of a special case! Instead of writing some return
            // value, we will update values on the native VST plugin's `AEffect`
            // object. This is triggered by the `audioMasterIOChanged` callback
            // from the hosted VST plugin.
            update_aeffect(*plugin, updated_plugin);

            return nullptr;
        },
        [](const DynamicSpeakerArrangement& speaker_arrangement)
            -> Vst2EventResult::Payload { return speaker_arrangement; },
        [&](const WantsAEffectUpdate&) -> Vst2EventResult::Payload {
            return *plugin;
        },
        [&](const WantsChunkBuffer&) -> Vst2EventResult::Payload {
            // In this case the plugin will have written its data stored in an
            // array to which a pointer is stored in `data`, with the return
            // value from the event determines how much data the plugin has
            // written
            const uint8_t* chunk_data = *static_cast<uint8_t**>(data);
            return ChunkData{
                std::vector<uint8_t>(chunk_data, chunk_data + return_value)};
        },
        [&](const WantsVstRect&) -> Vst2EventResult::Payload {
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
        [&](const WantsVstTimeInfo&) -> Vst2EventResult::Payload {
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
        [&](const WantsString&) -> Vst2EventResult::Payload {
            return std::string(static_cast<char*>(data));
        },
        [](const VstIOProperties& props) -> Vst2EventResult::Payload {
            return props;
        },
        [](const VstMidiKeyName& key_name) -> Vst2EventResult::Payload {
            return key_name;
        },
        [](const VstParameterProperties& props) -> Vst2EventResult::Payload {
            return props;
        }};

    // We'll need to serialize the result back depending on the payload type.
    // And as mentioned above, `effGetSpeakerArrangement()` wants the plugin's
    // speaker arrangement to be the data pointer, so we need to do this same
    // serialization step just for that function.
    const Vst2EventResult::Payload response_data =
        std::visit(write_payload_fn, event.payload);
    std::optional<Vst2EventResult::Payload> value_response_data = std::nullopt;
    if (event.value_payload) {
        value_response_data =
            std::visit(write_payload_fn, *event.value_payload);
    }

    return Vst2EventResult{.return_value = return_value,
                           .payload = response_data,
                           .value_payload = value_response_data};
}
