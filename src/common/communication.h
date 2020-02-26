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

#include <boost/asio/buffer.hpp>
#include <cinttypes>
#include <iostream>
#include <optional>

/**
 * The maximum size in bytes of a string or buffer passed through a void pointer
 * in one of the dispatch functions. This is used as a buffer size and also as a
 * cutoff for checking if c-style strings behind a `char*` have changed.
 */
constexpr size_t max_string_length = 128;

/**
 * The buffer size in bytes used for all buffers for sending and recieving
 * messages.
 *
 * TODO: This should probably depend on the type. 512 bytes is way too much for
 *       events, and probably not enough for sending audio.
 */
constexpr size_t buffer_size = 512;

// Types used for serialization and deserialization with bitsery.
template <std::size_t N>
using Buffer = std::array<u_int8_t, N>;

template <std::size_t N>
using OutputAdapter = bitsery::OutputBufferAdapter<Buffer<N>>;

template <std::size_t N>
using InputAdapter = bitsery::InputBufferAdapter<Buffer<N>>;

/**
 * An event as dispatched by the VST host. These events will get forwarded to
 * the VST host process running under Wine. The fields here mirror those
 * arguments sent to the `AEffect::dispatch` function.
 */
struct Event {
    int32_t opcode;
    int32_t index;
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

    template <typename S>
    void serialize(S& s) {
        s.value8b(return_value);
        s.ext(data, bitsery::ext::StdOptional(),
              [](S& s, auto& v) { s.text1b(v, max_string_length); });
    }
};

/**
 * Serialize an object using bitsery and write it to a socket.
 *
 * @param socket The Boost.Asio socket to write to.
 * @param object The object to write to the stream.
 *
 * @relates read_object
 */
template <typename T, typename Socket>
inline void write_object(Socket& socket, const T& object) {
    // TODO: Reuse buffers
    Buffer<buffer_size> buffer;
    auto length =
        bitsery::quickSerialization<OutputAdapter<buffer_size>>(buffer, object);

    socket.send(boost::asio::buffer(buffer, length));
}

/**
 * Deserialize an object by reading it from a socket. This should be used
 * together with `write_object`. This will block until the object is available.
 *
 * @param socket The Boost.Asio socket to read from.
 * @throw std::runtime_error If the conversion to an object was not successful.
 *
 * @relates write_object
 */
template <typename T, typename Socket>
inline T read_object(Socket& socket) {
    // TODO: Reuse buffers
    Buffer<buffer_size> buffer;
    auto message_length = socket.receive(boost::asio::buffer(buffer));

    T object;
    auto [_, success] =
        bitsery::quickDeserialization<InputAdapter<buffer_size>>(
            {buffer.begin(), message_length}, object);

    if (!success) {
        throw std::runtime_error("Deserialization failure in call:" +
                                 std::string(__PRETTY_FUNCTION__));
    }

    return object;
}
