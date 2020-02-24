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

#include <boost/asio/buffer.hpp>
#include <cinttypes>
#include <iostream>
#include <msgpack.hpp>
#include <optional>

/**
 * The maximum size in bytes of a string or buffer passed through a void pointer
 * in one of the dispatch functions. This is used as a buffer size and also as a
 * cutoff for checking if c-style strings behind a `char*` have changed.
 */
constexpr size_t max_string_size = 128;

/**
 * An event as dispatched by the VST host. These events will get forwarded to
 * the VST host process running under Wine. The fields here mirror those
 * arguments sent to the `AEffect::dispatch` function.
 */
struct Event {
    int32_t opcode;
    int32_t parameter;
    // TODO: This is an intptr_t, is this actually a poitner that should be
    //       dereferenced?
    intptr_t value;
    float option;
    /**
     * The event dispatch function has a void pointer parameter that's used to
     * either send string messages to the event (e.g. for `effCanDo`) or to
     * write a string back into. This value will contain an (empty) string if
     * the void* parameter for the dispatch function was not a null pointer.
     */
    std::optional<std::string> data;

    MSGPACK_DEFINE(opcode, parameter, value, option, data)
};

/**
 * AN instance of this should be sent back as a response to an incoming event.
 */
struct EventResult {
    /**
     * The result that should be returned from the dispatch function.
     */
    intptr_t return_value;
    /**
     * If present, this should get written into the void pointer passed to the
     * dispatch function.
     */
    std::optional<std::string> data;

    // TODO: Add missing return value fields;

    MSGPACK_DEFINE(return_value, data)
};

/**
 * Serialize an object and write it to a stream. This function prefixes the
 * output with the length of the serialized object in bytes since msgpack
 * doesn't handle this on its own.
 *
 * @param stream An ostream that can be written to.
 * @param object The object to write to the stream.
 *
 * @relates read_object
 */
template <typename T, typename Socket>
inline void write_object(Socket& socket, const T& object) {
    // TODO: Reuse buffers
    // TODO: Use boost's buffers directly after switching to bitsery
    msgpack::sbuffer buffer;
    msgpack::pack(buffer, object);

    socket.send(boost::asio::buffer(buffer.data(), buffer.size()));
}

/**
 * Deserialize an object by reading it from a stream. This should be used
 * together with `write_object`. This will block until the object is
 available.
 *
 * @param stream The stream to read from.
 * @throw msgpack::type_error If the conversion to an object was not successful.
 *
 * @relates write_object
 */
template <typename T, typename Socket>
inline T read_object(Socket& socket) {
    // TODO: Reuse buffers, also this is way too large right now
    // TODO: Use boost's buffers directly after switching to bitsery
    char buffer[4096];
    auto message_length = socket.receive(boost::asio::buffer(buffer));

    return msgpack::unpack(buffer, message_length).get().convert();
}
