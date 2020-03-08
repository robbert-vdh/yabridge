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

// I don't want to editor 'include/vestige/aeffectx.h`. That's why this type
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
    bitsery::ext::PointerLinkingContext serializer_context{};
    auto length =
        bitsery::quickSerialization<bitsery::ext::PointerLinkingContext,
                                    OutputAdapter<typename buffer_t<T>::type>>(
            serializer_context, buffer, object);

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

    bitsery::ext::PointerLinkingContext serializer_context{};
    auto [_, success] =
        bitsery::quickDeserialization<bitsery::ext::PointerLinkingContext,
                                      InputAdapter<typename buffer_t<T>::type>>(
            serializer_context, {buffer.begin(), message_length}, object);

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
 * @param is_dispatch whether or not this is for sending `dispatch()` events or
 *   host callbacks. Used for the serialization of opcode specific structs in
 *   the `dispatch()` function.
 * @param logger A logger instance. Optional since it doesn't have to be done on
 *   both sides. Optional references are somehow not possible in C++17 things
 *   like `std::reference_wrapper`, so a raw pointer it is.
 *
 * @relates passthrough_event
 */
intptr_t send_event(boost::asio::local::stream_protocol::socket& socket,
                    bool is_dispatch,
                    int opcode,
                    int index,
                    intptr_t value,
                    void* data,
                    float option,
                    Logger* logger);

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
    std::array<char, max_string_length> buffer;
    void* data = std::visit(
        overload{[&](const std::nullptr_t&) -> void* { return nullptr; },
                 [&](const std::string& s) -> void* {
                     return const_cast<char*>(s.c_str());
                 },
                 // TODO: Check if the deserialization leaks memory
                 [&](VstEvents& events) -> void* { return &events; },
                 [&](NeedsBuffer&) -> void* { return buffer.data(); }},
        event.payload);
    const intptr_t return_value = callback(plugin, event.opcode, event.index,
                                           event.value, data, event.option);

    // Only write back data for empty buffers
    // XXX: Is it possbile here that we got passed a non empty buffer (i.e.
    //      because it was not zeroed out by the host) for an event that should
    //      report some data back?
    const auto response_data =
        std::holds_alternative<NeedsBuffer>(event.payload)
            ? std::optional(std::string(static_cast<char*>(data)))
            : std::nullopt;

    if (logging.has_value()) {
        auto [logger, is_dispatch] = *logging;
        logger.log_event_response(is_dispatch, return_value, response_data);
    }

    EventResult response{return_value, response_data};
    write_object(socket, response);
}
