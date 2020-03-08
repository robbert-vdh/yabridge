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
 * @param logging A pair containing a logger instance and whether or not this is
 *   for sending `dispatch()` events or host callbacks. Optional since it
 *   doesn't have to be done on both sides.
 *
 * @relates passthrough_event
 */
intptr_t send_event(boost::asio::local::stream_protocol::socket& socket,
                    int opcode,
                    int index,
                    intptr_t value,
                    void* data,
                    float option,
                    std::optional<std::pair<Logger&, bool>> logging);
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
                         event.data, event.option);
    }

    // The void pointer argument for the dispatch function is used for
    // either:
    //  - Not at all, in which case it will be a null pointer
    //  - For passing strings as input to the event
    //  - For providing a buffer for the event to write results back into
    char* payload = nullptr;
    std::array<char, max_string_length> buffer;
    if (event.data.has_value()) {
        // If the data parameter was an empty string, then we're going to
        // pass a larger buffer to the dispatch function instead. Otherwise
        // we'll pass the data passed by the host.
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

    if (logging.has_value()) {
        auto [logger, is_dispatch] = *logging;
        logger.log_event_response(is_dispatch, return_value, response_data);
    }

    EventResult response{return_value, response_data};
    write_object(socket, response);
}
