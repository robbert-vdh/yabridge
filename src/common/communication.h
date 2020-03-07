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
#include <bitsery/ext/std_optional.h>
#include <bitsery/traits/array.h>
#include <bitsery/traits/string.h>
#include <bitsery/traits/vector.h>
#include <vestige/aeffect.h>

#include <cinttypes>
#include <iostream>
#include <optional>

#ifdef __WINE__
#include "../wine-host/boost-fix.h"
#endif
#include <boost/asio/local/stream_protocol.hpp>

#include "logging.h"

// These are for the serialization done by bitsery

/**
 * The maximum number of audio channels supported.
 */
constexpr size_t max_audio_channels = 32;
/**
 * The maximum number of samples in a buffer.
 */
constexpr size_t max_buffer_size = 16384;
/**
 * The maximum size in bytes of a string or buffer passed through a void pointer
 * in one of the dispatch functions. This is used as a buffer size and also as a
 * cutoff for checking if c-style strings behind a `char*` have changed.
 */
constexpr size_t max_string_length = 128;

/**
 * A simple constant sized buffer for smaller types that can be allocated on the
 * stack.
 */
template <std::size_t N>
using ArrayBuffer = std::array<uint8_t, N>;

template <typename B>
using OutputAdapter = bitsery::OutputBufferAdapter<B>;

template <typename B>
using InputAdapter = bitsery::InputBufferAdapter<B>;

/**
 * An event as dispatched by the VST host. These events will get forwarded to
 * the VST host process running under Wine. The fields here mirror those
 * arguments sent to the `AEffect::dispatch` function.
 */
struct Event {
    using buffer_type = ArrayBuffer<max_string_length + 32>;

    int32_t opcode;
    int32_t index;
    // TODO: This is an intptr_t, if we want to support 32 bit Wine plugins all
    //       of these these intptr_t types should be replace by `uint64_t` to
    //       remain compatible with the Linux VST plugin.
    intptr_t value;
    float option;
    /**
     * The event dispatch function has a void pointer parameter that's used to
     * either send string messages to the event (e.g. for `effCanDo`) or to
     * write a string back into. This value will contain an (empty) string if
     * the void* parameter for the dispatch function was not a null pointer.
     */
    std::optional<std::string> data;

    template <typename S>
    void serialize(S& s) {
        s.value4b(opcode);
        s.value4b(index);
        // Hard coding pointer sizes to 8 bytes should be fine, right? Even if
        // we're hosting a 32 bit plugin the native VST plugin will still use 64
        // bit large pointers.
        s.value8b(value);
        s.value4b(option);
        s.ext(data, bitsery::ext::StdOptional(),
              [](S& s, auto& v) { s.text1b(v, max_string_length); });
    }
};

/**
 * AN instance of this should be sent back as a response to an incoming event.
 */
struct EventResult {
    using buffer_type = ArrayBuffer<max_string_length + 32>;

    /**
     * The result that should be returned from the dispatch function.
     */
    intptr_t return_value;
    /**
     * If present, this should get written into the void pointer passed to the
     * dispatch function.
     */
    std::optional<std::string> data;

    template <typename S>
    void serialize(S& s) {
        s.value8b(return_value);
        s.ext(data, bitsery::ext::StdOptional(),
              [](S& s, auto& v) { s.text1b(v, max_string_length); });
    }
};

/**
 * Represents a call to either `getParameter` or `setParameter`, depending on
 * whether `value` contains a value or not.
 */
struct Parameter {
    using buffer_type = ArrayBuffer<16>;

    int32_t index;
    std::optional<float> value;

    template <typename S>
    void serialize(S& s) {
        s.value4b(index);
        s.ext(value, bitsery::ext::StdOptional(),
              [](S& s, auto& v) { s.value4b(v); });
    }
};

/**
 * The result of a `getParameter` or a `setParameter` call. For `setParameter`
 * this struct won't contain any values and mostly acts as an acknowledgement
 * from the Wine VST host.
 */
struct ParameterResult {
    using buffer_type = ArrayBuffer<16>;

    std::optional<float> value;

    template <typename S>
    void serialize(S& s) {
        s.ext(value, bitsery::ext::StdOptional(),
              [](S& s, auto& v) { s.value4b(v); });
    }
};

/**
 * A buffer of audio for the plugin to process, or the response of that
 * processing. The number of samples is encoded in each audio buffer's length.
 */
struct AudioBuffers {
    // When sending data we could use a vector of the right size, but when
    // receiving data we don't know how large this vector should be in advance
    // (or without sending the message length first)
    using buffer_type =
        ArrayBuffer<max_audio_channels * max_buffer_size * sizeof(float) + 16>;

    /**
     * An audio buffer for each of the plugin's audio channels.
     */
    std::vector<std::vector<float>> buffers;

    /**
     * The number of frames in a sample. If buffers is not empty, then
     * `buffers[0].size() == sample_frames`.
     */
    int sample_frames;

    template <typename S>
    void serialize(S& s) {
        s.container(buffers, max_audio_channels,
                    [](S& s, auto& v) { s.container4b(v, max_buffer_size); });
        s.value4b(sample_frames);
    }
};

/**
 * The serialization function for `AEffect` structs. This will s serialize all
 * of the values but it will not touch any of the pointer fields. That way you
 * can deserialize to an existing `AEffect` instance.
 */
template <typename S>
void serialize(S& s, AEffect& plugin) {
    s.value4b(plugin.magic);
    s.value4b(plugin.numPrograms);
    s.value4b(plugin.numParams);
    s.value4b(plugin.numInputs);
    s.value4b(plugin.numOutputs);
    s.value4b(plugin.flags);

    // These fields can contain some values that are rarely used and/or
    // deprecated, but we should pass them along anyway
    s.container1b(plugin.empty3);
    s.value4b(plugin.unknown_float);

    s.value4b(plugin.uniqueID);
    s.container1b(plugin.unknown1);
}

// I don't want to editor 'include/vestige/aeffect.h`. That's why this type
// trait and the above serialization function are here.` Clang complains that
// `buffer` should be qualified (and only in some cases), so `buffer_t` it is.
template <typename T>
struct buffer_t {
    using type = typename T::buffer_type;
};

template <>
struct buffer_t<AEffect> {
    using type = ArrayBuffer<128>;
};

/**
 * Serialize an object using bitsery and write it to a socket.
 *
 * @param socket The Boost.Asio socket to write to.
 * @param object The object to write to the stream.
 * @param buffer The buffer to write to. Only needed for when sending audio
 *   because their buffers might be quite large.
 *
 * @relates read_object
 */
template <typename T, typename Socket>
inline void write_object(Socket& socket,
                         const T& object,
                         typename buffer_t<T>::type& buffer) {
    auto length =
        bitsery::quickSerialization<OutputAdapter<typename buffer_t<T>::type>>(
            buffer, object);

    socket.send(boost::asio::buffer(buffer, length));
}

template <typename T, typename Socket>
inline void write_object(Socket& socket, const T& object) {
    typename buffer_t<T>::type buffer;
    write_object(socket, object, buffer);
}

/**
 * Deserialize an object by reading it from a socket. This should be used
 * together with `write_object`. This will block until the object is available.
 *
 * @param socket The Boost.Asio socket to read from.
 * @param object The object to deserialize to, if given. This can be used to
 *   update an existing `AEffect` struct without losing the pointers set by the
 *   host and the bridge.
 * @param buffer The buffer to write to. Only needed for when sending audio
 *   because their buffers might be quite large.
 *
 * @throw std::runtime_error If the conversion to an object was not successful.
 *
 * @relates write_object
 */
template <typename T, typename Socket>
inline T& read_object(Socket& socket,
                      T& object,
                      typename buffer_t<T>::type& buffer) {
    auto message_length = socket.receive(boost::asio::buffer(buffer));

    auto [_, success] =
        bitsery::quickDeserialization<InputAdapter<typename buffer_t<T>::type>>(
            {buffer.begin(), message_length}, object);

    if (!success) {
        throw std::runtime_error("Deserialization failure in call:" +
                                 std::string(__PRETTY_FUNCTION__));
    }

    return object;
}

template <typename T, typename Socket>
inline T& read_object(Socket& socket, T& object) {
    typename buffer_t<T>::type buffer;
    return read_object(socket, object, buffer);
}

template <typename T, typename Socket>
inline T read_object(Socket& socket) {
    T object;
    return read_object(socket, object);
}

/**
 * Serialize and send an event over a socket. This is used for both the host ->
 * plugin 'dispatch' events and the plugin -> host 'audioMaster' host callbacks
 * since they follow the same format. See one of those functions for details on
 * the parameters and return value of this function.
 *
 * @param logger The logger to optionally log the event to.
 * @param id_dispatch Whether this is for sending `dispatch()` events or host
 *   callbacks, used for debug logging purposes.
 *
 * @relates passthrough_event
 */
intptr_t send_event(boost::asio::local::stream_protocol::socket& socket,
                    int32_t opcode,
                    int32_t index,
                    intptr_t value,
                    void* data,
                    float option,
                    Logger& logger,
                    bool is_dispatch);

/**
 * Receive an event from a socket and pass it through to some callback function.
 * This is used for both the host -> plugin 'dispatch' events and the plugin ->
 * host 'audioMaster' host callbacks. This callback function is either one of
 * those functions.
 *
 * @param socket The socket to receive on and to send the response back to.
 * @param plugin The `AEffect` instance that should be passed to the callback
 *   function.
 * @param callback The function to call with the arguments received from the
 *   socket.
 * @param logger The logger to optionally log the event to.
 * @param id_dispatch Whether this is for sending `dispatch()` events or host
 *   callbacks, used for debug logging purposes.
 *
 * @relates send_event
 */
template <typename F>
void passthrough_event(boost::asio::local::stream_protocol::socket& socket,
                       AEffect* plugin,
                       F callback,
                       Logger& logger,
                       bool is_dispatch) {
    auto event = read_object<Event>(socket);
    logger.log_event(is_dispatch, event.opcode, event.index, event.value,
                     event.data, event.option);

    // The void pointer argument for the dispatch function is used for
    // either:
    //  - Not at all, in which case it will be a null pointer
    //  - For passing strings as input to the event
    //  - For providing a buffer for the event to write results back into
    char* payload = nullptr;
    std::array<char, max_string_length> buffer;
    if (event.data.has_value()) {
        // If the data parameter was an empty string, then we're going to pass a
        // larger buffer to the dispatch function instead. Otherwise we'll pass
        // the data passed by the host.
        if (!event.data->empty()) {
            payload = const_cast<char*>(event.data->c_str());
        } else {
            payload = buffer.data();
        }
    }

    const intptr_t return_value = callback(plugin, event.opcode, event.index,
                                           event.value, payload, event.option);

    // Only write back the value from `payload` if we were passed an empty
    // buffer to write into
    bool is_updated = event.data.has_value() && event.data->empty();
    const auto response_data =
        is_updated ? std::make_optional(payload) : std::nullopt;

    EventResult response{return_value, response_data};
    logger.log_event_response(is_dispatch, return_value, response_data);
    write_object(socket, response);
}
