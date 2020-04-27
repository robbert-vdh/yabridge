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

#include <mutex>

#include "communication.h"
#include "logging.h"

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
    virtual EventPayload read(const int /*opcode*/,
                              const int /*index*/,
                              const intptr_t /*value*/,
                              const void* data) {
        if (data == nullptr) {
            return nullptr;
        }

        // Assume buffers are zeroed out, this is probably not the case
        // FIXME: Some plugins, such as Fabfilter plugins, don't zero out their
        //        string buffers (such as when calling
        //        `audioMasterGetVendorString` and
        //        `audioMasterGetProductString`). We'll have to either manually
        //        specify which opcodes are expected to be strings or always
        //        write back changed strings. I think the first choice is
        //        cleaner.
        const char* c_string = static_cast<const char*>(data);
        if (c_string[0] != 0) {
            return std::string(c_string);
        } else {
            return WantsString{};
        }
    }

    /**
     * Write the reponse back to the data pointer.
     */
    virtual void write(const int /*opcode*/,
                       void* data,
                       const EventResult& response) {
        // The default behavior is to handle this as a null terminated C-style
        // string
        std::visit(overload{[&](const auto&) {},
                            [&](const std::string& s) {
                                char* output = static_cast<char*>(data);

                                // We use std::string for easy transport but in
                                // practice we're always writing null terminated
                                // C-style strings
                                std::copy(s.begin(), s.end(), output);
                                output[s.size()] = 0;
                            }},
                   response.payload);
    }

    /**
     * This function can override a callback's return value based on the opcode.
     * This is used in one place to return a pointer to a `VstTime` object
     * that's contantly being updated.
     *
     * @param opcode The opcode for the current event.
     * @param original The original return value as returned by the callback
     *   function.
     */
    virtual intptr_t return_value(const int /*opcode*/,
                                  const intptr_t original) {
        return original;
    }
};

/**
 * Serialize and send an event over a socket. This is used for both the host ->
 * plugin 'dispatch' events and the plugin -> host 'audioMaster' host callbacks
 * since they follow the same format. See one of those functions for details on
 * the parameters and return value of this function.
 *
 * @param socket The socket to write over, should be the same socket the other
 *   endpoint is using to call `passthrough_event()`.
 * @param write_mutex A mutex to ensure that only one thread can write to
 *   the socket at once. Needed because VST hosts and plugins can and sometimes
 *   will call the `dispatch()` or `audioMaster()` functions from multiple
 *   threads at once.
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
                    std::mutex& write_mutex,
                    D& data_converter,
                    std::optional<std::pair<Logger&, bool>> logging,
                    int opcode,
                    int index,
                    intptr_t value,
                    void* data,
                    float option) {
    // Encode the right payload type for this event. Check the documentation for
    // `EventPayload` for more information
    const EventPayload payload =
        data_converter.read(opcode, index, value, data);

    if (logging.has_value()) {
        auto [logger, is_dispatch] = logging.value();
        logger.log_event(is_dispatch, opcode, index, value, payload, option);
    }

    const Event event{opcode, index, value, option, payload};

    // Prevent two threads from writing over the socket at the same time and
    // messages getting out of order. This is needed because we can't prevent
    // the plugin or the host from calling `dispatch()` or `audioMaster()` from
    // multiple threads.
    EventResult response;
    {
        std::lock_guard lock(write_mutex);
        write_object(socket, event);
        response = read_object<EventResult>(socket);
    }

    if (logging.has_value()) {
        auto [logger, is_dispatch] = logging.value();
        logger.log_event_response(is_dispatch, opcode, response.return_value,
                                  response.payload);
    }

    data_converter.write(opcode, data, response);

    return data_converter.return_value(opcode, response.return_value);
}

/**
 * Receive an event from a socket and pass it through to some callback function.
 * This is used for both the host -> plugin 'dispatch' events and the plugin ->
 * host 'audioMaster' host callbacks. This callback function is either one of
 * those functions.
 *
 * @param socket The socket to receive on and to send the response back to.
 * @param logging A pair containing a logger instance and whether or not this is
 *   for sending `dispatch()` events or host callbacks. Optional since it
 *   doesn't have to be done on both sides.
 * @param plugin The `AEffect` instance that should be passed to the callback
 *   function.
 * @param callback The function to call with the arguments received from the
 *   socket.
 *
 * @relates send_event
 */
template <typename F>
void passthrough_event(boost::asio::local::stream_protocol::socket& socket,
                       std::optional<std::pair<Logger&, bool>> logging,
                       AEffect* plugin,
                       F callback) {
    auto event = read_object<Event>(socket);
    if (logging.has_value()) {
        auto [logger, is_dispatch] = logging.value();
        logger.log_event(is_dispatch, event.opcode, event.index, event.value,
                         event.payload, event.option);
    }

    // This buffer is used to write strings and small objects to. We'll
    // initialize it with a single null to prevent it from being read as some
    // arbitrary C-style string.
    std::array<char, max_string_length> string_buffer;
    string_buffer[0] = 0;
    // This buffer is only used for retrieving chunk data and will be allocated
    // as needed
    std::vector<uint8_t> binary_buffer;

    void* data = std::visit(
        overload{[&](const std::nullptr_t&) -> void* { return nullptr; },
                 [&](const std::string& s) -> void* {
                     return const_cast<char*>(s.c_str());
                 },
                 [&](const std::vector<uint8_t>& buffer) -> void* {
                     return const_cast<uint8_t*>(buffer.data());
                 },
                 [&](size_t& window_handle) -> void* {
                     // This is the X11 window handle that the editor should
                     // reparent itself to. We have a special wrapper around the
                     // dispatch function that intercepts `effEditOpen` events
                     // and creates a Win32 window and then finally embeds the
                     // X11 window Wine created into this wnidow handle.
                     return reinterpret_cast<void*>(window_handle);
                 },
                 [&](const AEffect&) -> void* { return nullptr; },
                 [&](DynamicVstEvents& events) -> void* {
                     return &events.as_c_events();
                 },
                 [&](WantsChunkBuffer&) -> void* {
                     binary_buffer.resize(binary_buffer_size);
                     return binary_buffer.data();
                 },
                 [&](VstIOProperties& props) -> void* { return &props; },
                 [&](VstParameterProperties& props) -> void* { return &props; },
                 [&](WantsVstRect&) -> void* { return string_buffer.data(); },
                 [&](const WantsVstTimeInfo&) -> void* { return nullptr; },
                 [&](WantsString&) -> void* { return string_buffer.data(); }},
        event.payload);

    const intptr_t return_value = callback(plugin, event.opcode, event.index,
                                           event.value, data, event.option);

    // Only write back data when needed, this depends on the event payload type
    const auto response_data = std::visit(
        overload{[&](auto) -> EventResposnePayload { return nullptr; },
                 [&](const AEffect& updated_plugin) -> EventResposnePayload {
                     // This is a bit of a special case! Instead of writing some
                     // return value, we will update values on the native VST
                     // plugin's `AEffect` object. This is triggered by the
                     // `audioMasterIOChanged` callback from the hsoted VST
                     // plugin.

                     // These are the same fields written by bitsery in the
                     // initialization of `HostBridge`. I can't think of a way t
                     // oreuse the serializer without first having to serialize
                     // `updated_plugin` first though.
                     plugin->magic = updated_plugin.magic;
                     plugin->numPrograms = updated_plugin.numPrograms;
                     plugin->numParams = updated_plugin.numParams;
                     plugin->numInputs = updated_plugin.numInputs;
                     plugin->numOutputs = updated_plugin.numOutputs;
                     plugin->flags = updated_plugin.flags;
                     plugin->initialDelay = updated_plugin.initialDelay;
                     plugin->empty3a = updated_plugin.empty3a;
                     plugin->empty3b = updated_plugin.empty3b;
                     plugin->unkown_float = updated_plugin.unkown_float;
                     plugin->uniqueID = updated_plugin.uniqueID;
                     plugin->version = updated_plugin.version;

                     return nullptr;
                 },
                 [&](WantsChunkBuffer&) -> EventResposnePayload {
                     // In this case the plugin will have written its data
                     // stored in an array to which a pointer is stored in
                     // `data`, with the return value from the event determines
                     // how much data the plugin has written
                     const uint8_t* chunk_data = *static_cast<uint8_t**>(data);
                     return std::vector<uint8_t>(chunk_data,
                                                 chunk_data + return_value);
                 },
                 [&](VstIOProperties& props) -> EventResposnePayload {
                     return props;
                 },
                 [&](VstParameterProperties& props) -> EventResposnePayload {
                     return props;
                 },
                 [&](WantsVstRect&) -> EventResposnePayload {
                     // The plugin has written a pointer to a VstRect struct
                     // into the data poitner
                     return **static_cast<VstRect**>(data);
                 },
                 [&](WantsVstTimeInfo&) -> EventResposnePayload {
                     // Not sure why the VST API has twenty different ways of
                     // returning structs, but in this case the value returned
                     // from the callback function is actually a pointer to a
                     // `VstTimeInfo` struct! It can also be a null pointer if
                     // the host doesn't support this.
                     const auto time_info =
                         reinterpret_cast<const VstTimeInfo*>(return_value);
                     if (time_info == nullptr) {
                         return nullptr;
                     } else {
                         return *time_info;
                     }
                 },
                 [&](WantsString&) -> EventResposnePayload {
                     return std::string(static_cast<char*>(data));
                 }},
        event.payload);

    if (logging.has_value()) {
        auto [logger, is_dispatch] = logging.value();
        logger.log_event_response(is_dispatch, event.opcode, return_value,
                                  response_data);
    }

    EventResult response{return_value, response_data};
    write_object(socket, response);
}
