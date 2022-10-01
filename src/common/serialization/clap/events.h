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

#include <string>
#include <variant>

#include <bitsery/traits/string.h>
#include <clap/events.h>

#include "../bitsery/ext/in-place-variant.h"
#include "../bitsery/ext/native-pointer.h"
#include "../common.h"

namespace clap {
namespace events {

/**
 * The actual event data. `clap::events::Event` stores these as a variant.
 * Ideally we'd store only the non-header payload data, but the
 * `clap_input_events::get()` function requires us to return a pointer to the
 * header, so if we did that then we'd need to create a second buffer containing
 * the serialzed events.
 */
namespace payload {

/**
 * The payload for `clap_event_note`. This is used for multiple event types,
 * which are encoded through `event.header.type`.
 */
struct Note {
    clap_event_note_t event;

    template <typename S>
    void serialize(S& s) {
        s.object(event.header);
        s.value4b(event.note_id);
        s.value2b(event.port_index);
        s.value2b(event.channel);
        s.value2b(event.key);
        s.value8b(event.velocity);
    }
};

/**
 * The payload for `clap_event_note_expression`.
 */
struct NoteExpression {
    clap_event_note_expression_t event;

    template <typename S>
    void serialize(S& s) {
        s.object(event.header);
        s.value4b(event.expression_id);
        s.value4b(event.note_id);
        s.value2b(event.port_index);
        s.value2b(event.channel);
        s.value2b(event.key);
        s.value8b(event.value);
    }
};

/**
 * The payload for `clap_event_param_value`.
 */
struct ParamValue {
    clap_event_param_value_t event;

    template <typename S>
    void serialize(S& s) {
        s.object(event.header);
        s.value4b(event.param_id);
        // The cookie is a pointer. Using `native_size_t`/the host system's
        // pointer size here will allow bridged 32-bit plugins to work
        // correctly.
        // XXX: This will silently blow up when using 32-bit yabridge on a
        //      64-bit system with 64-bit plugins, but that's such a specific
        //      use case that we won't even bother. Building 32-bit yabridge
        //      with CLAP support on 64-bit symbols has been disabled to prevent
        //      this from being an issue.
        s.ext(event.cookie, bitsery::ext::NativePointer{});
        s.value4b(event.note_id);
        s.value2b(event.port_index);
        s.value2b(event.channel);
        s.value2b(event.key);
        s.value8b(event.value);
    }
};

/**
 * The payload for `clap_event_param_mod`.
 */
struct ParamMod {
    clap_event_param_mod_t event;

    template <typename S>
    void serialize(S& s) {
        s.object(event.header);
        s.value4b(event.param_id);
        // Same as the above
        s.ext(event.cookie, bitsery::ext::NativePointer{});
        s.value4b(event.note_id);
        s.value2b(event.port_index);
        s.value2b(event.channel);
        s.value2b(event.key);
        s.value8b(event.amount);
    }
};

/**
 * The payload for `clap_event_param_gesture`. This is used for multiple event
 * types, which are encoded through `event.header.type`.
 */
struct ParamGesture {
    clap_event_param_gesture_t event;

    template <typename S>
    void serialize(S& s) {
        s.object(event.header);
        s.value4b(event.param_id);
    }
};

/**
 * The payload for `clap_event_transport`.
 */
struct Transport {
    clap_event_transport_t event;

    template <typename S>
    void serialize(S& s) {
        s.object(event.header);
        s.value4b(event.flags);
        s.value8b(event.song_pos_beats);
        s.value8b(event.song_pos_seconds);
        s.value8b(event.tempo);
        s.value8b(event.tempo_inc);
        s.value8b(event.loop_start_beats);
        s.value8b(event.loop_end_beats);
        s.value8b(event.loop_start_seconds);
        s.value8b(event.loop_end_seconds);
        s.value8b(event.bar_start);
        s.value4b(event.bar_number);
        s.value2b(event.tsig_num);
        s.value2b(event.tsig_denom);
    }
};

/**
 * The payload for `clap_event_midi`.
 */
struct Midi {
    clap_event_midi_t event;

    template <typename S>
    void serialize(S& s) {
        s.object(event.header);
        s.value2b(event.port_index);
        s.container1b(event.data);
    }
};

/**
 * The payload for `clap_event_midi_sysex`.
 */
struct MidiSysex {
    clap_event_midi_sysex_t event;

    /**
     * The actual SysEx event data. The pointer in `event` is set to the string
     * data after the event has been created. As long as this event is not moved
     * that pointer will remain valid.
     *
     * We're not expecting a lot of SysEx events, and `std::string`'s small
     * string optimization should make it possible to send small sysex events
     * without allocations. An alternative that won't allocate as quickly would
     * be to store the data in a vector and to only store a tag here, but I
     * don't think it's necessary at the moment.
     */
    std::string buffer;

    template <typename S>
    void serialize(S& s) {
        s.object(event.header);
        s.value2b(event.port_index);

        s.text1b(buffer, 1 << 16);

        // NOTE: These will need to be set when retrieving the event using
        //       `clap_input_events::get()`. We could set the pointer here, but
        //       in the off chance that there are a lot more events than we can
        //       handle and the vector is reallocated to avoid dropping events,
        //       then these pointers would become dangling. Making sure these
        //       are null until the event is retrieved is probably for the best.
        event.buffer = nullptr;
        event.size = 0;
    }
};

/**
 * The payload for `clap_event_midi2`.
 */
struct Midi2 {
    clap_event_midi2_t event;

    template <typename S>
    void serialize(S& s) {
        s.object(event.header);
        s.value2b(event.port_index);
        s.container4b(event.data);
    }
};

}  // namespace payload

/**
 * Encodes a CLAP event. These can be parsed from a `clap_event_header_t*` and
 * reconstructed back to a `clap_event_header`.
 */
struct alignas(16) Event {
    /**
     * Parse a CLAP event. Returns a nullopt if yabridge does not support the
     * event.
     */
    static std::optional<Event> parse(const clap_event_header_t& generic_event);

    /**
     * Get the `clap_event_header_t*` representation for this event. The pointer
     * is valid as long as this struct isn't moved.
     */
    const clap_event_header_t* get() const;

    /**
     * The actual event data. These also contain the header because storing the
     * entire `clap_event_*_t` struct is the only way to serialize the event
     * list in a way that doesn't require us to create a second event list in
     * that format after deserializing the events. An alternative would be to
     * write the event in the proper format to a buffer before returning it from
     * `clap_input_events::get()`, but that would cause unexpected lifetime
     * issues.
     */
    mutable std::variant<
        payload::Note,
        payload::NoteExpression,
        payload::ParamValue,
        payload::ParamMod,
        payload::ParamGesture,
        // Most events are about the same length, but having the transport in
        // here sadly doubles this struct's size
        // TODO: Pack the events at some point, this will require special
        //       handling for SysEx events
        payload::Transport,
        payload::Midi,
        payload::MidiSysex,
        payload::Midi2>
        payload;

    template <typename S>
    void serialize(S& s) {
        s.ext(payload, bitsery::ext::InPlaceVariant{});
    }
};

}  // namespace events
}  // namespace clap

template <typename S>
void serialize(S& s, clap_event_header_t& event_header) {
    // Feels a bit weird serializing this, but assuming the host/plugin set it
    // correctly it will be fine. And this is kind of a host implementation
    // detail for storing the events in a packed list anyways.
    s.value4b(event_header.size);
    s.value4b(event_header.time);
    s.value2b(event_header.space_id);
    s.value2b(event_header.type);
    s.value4b(event_header.flags);
}
