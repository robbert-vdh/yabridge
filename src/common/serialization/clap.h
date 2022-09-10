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

#include <variant>

#include "../bitsery/ext/in-place-variant.h"

#include "../bitsery/ext/message-reference.h"
#include "../utils.h"
#include "clap/host.h"
#include "clap/plugin-factory.h"
#include "common.h"

// The CLAP communication strategy is identical to what we do for VST3.

// The messages are defined in the `clap/` directory following the same
// structure as the CLAP repo.

/**
 * When we send a control message from the plugin to the Wine plugin host, this
 * encodes the information we request or the operation we want to perform. A
 * request of type `ClapControlRequest(T)` should send back a `T::Response`.
 * These messages are for main thread functions.
 */
// FIXME: Remove the `WantsConfiguration`. For some reason bitsery just won't
//        serialize this without it.
using ClapMainThreadControlRequest = std::variant<WantsConfiguration,
                                                  clap::plugin_factory::List,
                                                  clap::plugin_factory::Create,
                                                  clap::plugin::Init,
                                                  clap::plugin::Destroy>;

template <typename S>
void serialize(S& s, ClapMainThreadControlRequest& payload) {
    // All of the objects in the variant should have their own serialization
    // function
    s.ext(payload, bitsery::ext::InPlaceVariant{});
}

/**
 * A message type for audio thread functions the host can call on the plugin.
 * These functions are called from a hot loop every processing cycle, so we'll
 * have a dedicated socket for these for every plugin instance.
 *
 * This is wrapped in a struct so we can use some bitsery magic to deserialize
 * to a reference. This object is kept around as a thread local, and the
 * `process_request_` field stores the last process request received. This
 * allows other functions to be called in between process calls without having
 * to recreate this object. See `MessageReference<T>` and
 * `bitsery::ext::MessageReference<T>` for more information.
 */
struct ClapAudioThreadControlRequest {
    ClapAudioThreadControlRequest() {}

    /**
     * Initialize the variant with an object. In `ClapSockets::send_message()`
     * the object gets implicitly converted to the this variant.
     */
    template <typename T>
    ClapAudioThreadControlRequest(T request) : payload(std::move(request)) {}

    // TODO:
    using Payload = std::variant<WantsConfiguration>;

    Payload payload;

    template <typename S>
    void serialize(S& s) {
        s.ext(
            payload,
            bitsery::ext::InPlaceVariant{
                // TODO: Process data
                // [&](S& s,
                //     MessageReference<YaAudioProcessor::Process>& request_ref)
                //     {
                //     // When serializing this reference we'll read the data
                //     // directly from the referred to object. During
                //     // deserializing we'll deserialize into the persistent and
                //     // thread local `process_request` object (see
                //     // `ClapSockets::add_audio_processor_and_listen`) and then
                //     // reassign the reference to point to that object.
                //     s.ext(request_ref,
                //           bitsery::ext::MessageReference(process_request_));
                // },
                [](S& s, auto& request) { s.object(request); }});
    }

    // TODO: Process data, update docstring
    // /**
    //  * Used for deserializing the
    //  `MessageReference<YaAudioProcessor::Process>`
    //  * variant. When we encounter this variant, we'll actually deserialize
    //  the
    //  * object into this object, and we'll then reassign the reference to
    //  point
    //  * to this object. That way we can keep it around as a thread local
    //  object
    //  * to prevent unnecessary allocations.
    //  */
    // std::optional<YaAudioProcessor::Process> process_request_;
};

/**
 * When we do a callback from the Wine plugin host to the plugin, this encodes
 * the information we want or the operation we want to perform. A request of
 * type `ClapMainThreadCallbackRequest(T)` should send back a `T::Response`.
 */
// TODO: Placeholder
using ClapMainThreadCallbackRequest = std::variant<WantsConfiguration>;

template <typename S>
void serialize(S& s, ClapMainThreadCallbackRequest& payload) {
    // All of the objects in `ClapMainThreadCallbackRequest` should have their
    // own serialization function.
    s.ext(payload, bitsery::ext::InPlaceVariant{});
}

/**
 * Fetch the `std::variant<>` from an audio thread request object. This will
 * let us use our regular, simple function call dispatch code, but we can still
 * store the process data in a separate field (to reduce allocations).
 *
 * @overload
 */
inline ClapAudioThreadControlRequest::Payload& get_request_variant(
    ClapAudioThreadControlRequest& request) noexcept {
    return request.payload;
}
