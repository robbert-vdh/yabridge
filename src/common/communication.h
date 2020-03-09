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

#include <bitsery/bitsery.h>

#include <cinttypes>
#include <iostream>
#include <optional>

#ifdef __WINE__
#include "../wine-host/boost-fix.h"
#endif
#include <boost/asio/local/stream_protocol.hpp>

#include "logging.h"
#include "serialization.h"

template <typename B>
using OutputAdapter = bitsery::OutputBufferAdapter<B>;

template <typename B>
using InputAdapter = bitsery::InputBufferAdapter<B>;

/**
 * Serialize an object using bitsery and write it to a socket.
 *
 * @param socket The Boost.Asio socket to write to.
 * @param object The object to write to the stream.
 * @param buffer The buffer to write to. This is useful for sending audio and
 *   chunk data since that can vary in size by a lot.
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
    socket.send(boost::asio::buffer(std::array<size_t, 1>{size}));
    socket.send(boost::asio::buffer(buffer, size));
}

/**
 * Deserialize an object by reading it from a socket. This should be used
 * together with `write_object`. This will block until the object is available.
 *
 * @param socket The Boost.Asio socket to read from.
 * @param object The object to deserialize to, if given. This can be used to
 *   update an existing `AEffect` struct without losing the pointers set by the
 *   host and the bridge.
 * @param buffer The buffer to read into. This is useful for sending audio and
 *   chunk data since that can vary in size by a lot.
 *
 * @throw std::runtime_error If the conversion to an object was not successful.
 *
 * @relates write_object
 */
template <typename T, typename Socket>
inline T& read_object(Socket& socket,
                      T& object,
                      std::vector<uint8_t> buffer = std::vector<uint8_t>(64)) {
    std::array<size_t, 1> message_length;
    socket.receive(boost::asio::buffer(message_length));

    // Make sure the buffer is large enough
    const size_t size = message_length[0];
    buffer.resize(size);

    const auto actual_size = socket.receive(boost::asio::buffer(buffer));
    assert(size == actual_size);

    auto [_, success] =
        bitsery::quickDeserialization<InputAdapter<std::vector<uint8_t>>>(
            {buffer.begin(), size}, object);

    if (BOOST_UNLIKELY(!success)) {
        throw std::runtime_error("Deserialization failure in call:" +
                                 std::string(__PRETTY_FUNCTION__));
    }

    return object;
}

template <typename T, typename Socket>
inline T read_object(Socket& socket) {
    T object;
    return read_object(socket, object);
}

/**
 * Encodes the base behavior for reading from and writing to the `data` argument
 * for event dispatch functions. This is sufficient for host callbacks
 * (`audioMaster()`). The `dispatch()` function will require some more specific
 * structs.
 */
class DefaultDataConverter {
   public:
    EventPayload read(const int /*opcode*/, const void* data) {
        if (data == nullptr) {
            return nullptr;
        }

        // Assume buffers are zeroed out, this is probably not the case
        const char* c_string = static_cast<const char*>(data);
        if (c_string[0] != 0) {
            return std::string(c_string);
        } else {
            return WantsString{};
        }
    }

    void write(const int /*opcode*/, void* data, const EventResult& response) {
        if (response.data.has_value()) {
            char* output = static_cast<char*>(data);

            // For correctness we will copy the entire buffer and add a
            // terminating null byte ourselves. In practice `response.data` will
            // only ever contain C-style strings, but this would work with any
            // other data format that can contain null bytes.
            std::copy(response.data->begin(), response.data->end(), output);
            output[response.data->size()] = 0;
        }
    }
};

/**
 * Serialize and send an event over a socket. This is used for both the host ->
 * plugin 'dispatch' events and the plugin -> host 'audioMaster' host callbacks
 * since they follow the same format. See one of those functions for details on
 * the parameters and return value of this function.
 *
 * @param data_converter Some struct that knows how to read data from and write
 *   data back to the `data` void pointer. For host callbacks this parameter
 *   contains either a string or a null pointer while `dispatch()` calls might
 *   contain opcode specific structs. See the documentation for `EventPayload`
 *   for more information. The `DefaultDataConverter` defined above handles the
 *   basic behavior that's sufficient for hsot callbacks.
 * @param logging A pair containing a logger instance and whether or not this is
 *   for sending `dispatch()` events or host callbacks. Optional since it
 *   doesn't have to be done on both sides.
 *
 * @relates passthrough_event
 */
template <typename D>
intptr_t send_event(boost::asio::local::stream_protocol::socket& socket,
                    D& data_converter,
                    int opcode,
                    int index,
                    intptr_t value,
                    void* data,
                    float option,
                    std::optional<std::pair<Logger&, bool>> logging) {
    // Encode the right payload type for this event. Check the documentation for
    // `EventPayload` for more information.
    const EventPayload payload = data_converter.read(opcode, data);

    if (logging.has_value()) {
        auto [logger, is_dispatch] = *logging;
        logger.log_event(is_dispatch, opcode, index, value, payload, option);
    }

    const Event event{opcode, index, value, option, payload};
    write_object(socket, event);

    const auto response = read_object<EventResult>(socket);

    if (logging.has_value()) {
        auto [logger, is_dispatch] = *logging;
        logger.log_event_response(is_dispatch, response.return_value,
                                  response.data);
    }

    data_converter.write(opcode, data, response);

    return response.return_value;
}

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
 * @param logging A pair containing a logger instance and whether or not this is
 *   for sending `dispatch()` events or host callbacks. Optional since it
 *   doesn't have to be done on both sides.
 *
 * @relates send_event
 */
template <typename F>
void passthrough_event(boost::asio::local::stream_protocol::socket& socket,
                       AEffect* plugin,
                       F callback,
                       std::optional<std::pair<Logger&, bool>> logging) {
    auto event = read_object<Event>(socket);
    if (logging.has_value()) {
        auto [logger, is_dispatch] = *logging;
        logger.log_event(is_dispatch, event.opcode, event.index, event.value,
                         event.payload, event.option);
    }

    // Some buffer for the event to write to if needed. We only pass around a
    // marker struct to indicate that this is indeed the case.
    std::vector<char> binary_buffer;
    std::array<char, max_string_length> string_buffer;
    void* data = std::visit(
        overload{[&](const std::nullptr_t&) -> void* { return nullptr; },
                 [&](const std::string& s) -> void* {
                     return const_cast<char*>(s.c_str());
                 },
                 [&](DynamicVstEvents& events) -> void* {
                     return &events.as_c_events();
                 },
                 [&](WantsBinaryBuffer&) -> void* {
                     // Only allocate when we actually need this, i.e. when
                     // we're getting a chunk from the plugin
                     binary_buffer.resize(binary_buffer_size);
                     return binary_buffer.data();
                 },
                 [&](WantsString&) -> void* { return string_buffer.data(); }},
        event.payload);

    const intptr_t return_value = callback(plugin, event.opcode, event.index,
                                           event.value, data, event.option);

    // Only write back data when needed, this depends on the event payload type
    // XXX: Is it possbile here that we got passed a non empty buffer (i.e.
    //      because it was not zeroed out by the host) for an event that should
    //      report some data back?
    const auto response_data = std::visit(
        overload{
            [&](WantsBinaryBuffer&) -> std::optional<std::string> {
                // In this case the return value from the event determines how
                // much data the plugin has written
                return std::string(static_cast<char*>(data), return_value);
            },
            [&](WantsString&) -> std::optional<std::string> {
                return std::string(static_cast<char*>(data));
            },
            [&](auto) -> std::optional<std::string> { return std::nullopt; }},
        event.payload);

    if (logging.has_value()) {
        auto [logger, is_dispatch] = *logging;
        logger.log_event_response(is_dispatch, return_value, response_data);
    }

    EventResult response{return_value, response_data};
    write_object(socket, response);
}
