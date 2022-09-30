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
#include "../common.h"

namespace clap {
namespace events {

/**
 * The actual event data. `clap::events::Event` stores these as a variant
 * alongside the timestamp and flags for the original event header.
 */
namespace payload {

/**
 * `Note` can be a variety of note events.
 */
enum class NoteEventType : uint8_t {
    On,
    Off,
    Choke,
    End,
};

/**
 * The payload for `clap_event_note`.
 */
struct Note {
    NoteEventType event_type;

    int32_t note_id;
    int16_t port_index;
    int16_t channel;
    int16_t key;

    double velocity;

    template <typename S>
    void serialize(S& s) {
        s.value1b(event_type);
        s.value4b(note_id);
        s.value2b(port_index);
        s.value2b(channel);
        s.value2b(key);
        s.value8b(velocity);
    }
};

/**
 * The payload for `clap_event_note_expression`.
 */
struct NoteExpression {
    clap_note_expression expression_id;

    int32_t note_id;
    int16_t port_index;
    int16_t channel;
    int16_t key;

    double value;

    template <typename S>
    void serialize(S& s) {
        s.value4b(expression_id);
        s.value4b(note_id);
        s.value2b(port_index);
        s.value2b(channel);
        s.value2b(key);
        s.value8b(value);
    }
};

/**
 * The payload for `clap_event_param_value`.
 */
struct ParamValue {
    clap_id param_id;
    // This is a pointer. Using `native_size_t`/the host system's pointer size
    // here will allow bridged 32-bit plugins to work correctly.
    // XXX: This will silently blow up when using 32-bit yabridge on a 64-bit
    //      system with 64-bit plugins, but that's such a specific use case that
    //      we won't even bother.
    native_size_t cookie;

    int32_t note_id;
    int16_t port_index;
    int16_t channel;
    int16_t key;

    double value;

    template <typename S>
    void serialize(S& s) {
        s.value4b(param_id);
        s.value8b(cookie);
        s.value4b(note_id);
        s.value2b(port_index);
        s.value2b(channel);
        s.value2b(key);
        s.value8b(value);
    }
};

/**
 * The payload for `clap_event_param_mod`.
 */
struct ParamMod {
    clap_id param_id;
    // Same as above
    native_size_t cookie;

    int32_t note_id;
    int16_t port_index;
    int16_t channel;
    int16_t key;

    double amount;

    template <typename S>
    void serialize(S& s) {
        s.value4b(param_id);
        s.value8b(cookie);
        s.value4b(note_id);
        s.value2b(port_index);
        s.value2b(channel);
        s.value2b(key);
        s.value8b(amount);
    }
};

/**
 * `ParamGesture` can both be a `CLAP_EVENT_PARAM_GESTURE_BEGIN` and a
 * `CLAP_EVENT_PARAM_GESTURE_END`.
 */
enum class ParamGestureType : uint8_t {
    Begin,
    End,
};

/**
 * The payload for `clap_event_param_gesture`.
 */
struct ParamGesture {
    ParamGestureType gesture_type;
    clap_id param_id;

    template <typename S>
    void serialize(S& s) {
        s.value1b(gesture_type);
        s.value4b(param_id);
    }
};

/**
 * The payload for `clap_event_transport`.
 */
struct Transport {
    uint32_t flags;

    clap_beattime song_pos_beats;
    clap_sectime song_pos_seconds;

    double tempo;
    double tempo_inc;

    clap_beattime loop_start_beats;
    clap_beattime loop_end_beats;
    clap_sectime loop_start_seconds;
    clap_sectime loop_end_seconds;

    clap_beattime bar_start;
    int32_t bar_number;

    uint16_t tsig_num;
    uint16_t tsig_denom;

    template <typename S>
    void serialize(S& s) {
        s.value4b(flags);
        s.value8b(song_pos_beats);
        s.value8b(song_pos_seconds);
        s.value8b(tempo);
        s.value8b(tempo_inc);
        s.value8b(loop_start_beats);
        s.value8b(loop_end_beats);
        s.value8b(loop_start_seconds);
        s.value8b(loop_end_seconds);
        s.value8b(bar_start);
        s.value4b(bar_number);
        s.value2b(tsig_num);
        s.value2b(tsig_denom);
    }
};

/**
 * The payload for `clap_event_midi`.
 */
struct Midi {
    uint16_t port_index;
    uint8_t data[3];

    template <typename S>
    void serialize(S& s) {
        s.value2b(port_index);
        s.container4b(data);
    }
};

/**
 * The payload for `clap_event_midi_sysex`.
 */
struct MidiSysex {
    uint16_t port_index;
    // We're not expecting a lot of SysEx events, and `std::string`'s small
    // string optimization should make it possible to send small sysex events
    // without allocations. An alternative that won't allocate as quickly would
    // be to store the data in a vector and to only store a tag here, but I
    // don't think it's necessary at the moment.
    std::string buffer;

    template <typename S>
    void serialize(S& s) {
        s.value2b(port_index);
        s.text1b(buffer, 1 << 16);
    }
};

/**
 * The payload for `clap_event_midi2`.
 */
struct Midi2 {
    uint16_t port_index;
    uint32_t data[4];

    template <typename S>
    void serialize(S& s) {
        s.value2b(port_index);
        s.container4b(data);
    }
};

}  // namespace payload

/**
 * Encodes a CLAP event. These can be parsed from a `clap_event_header_t*` and
 * reconstructed back to a `clap_event_header`.
 */
struct alignas(16) Event {
    /**
     * The time from the event header.
     */
    uint32_t time;
    /**
     * The flags from the event header.
     */
    uint32_t flags;

    /**
     * The actual event data. This also encodes the type, size, and space ID.
     */
    std::variant<payload::Note,
                 payload::NoteExpression,
                 payload::ParamValue,
                 payload::ParamMod,
                 payload::ParamGesture,
                 // Most events are about the same length, but having the
                 // transport in here sadly doubles this struct's size
                 // TODO: Pack the events at some point, this will require
                 //       special handling for SysEx events
                 payload::Transport,
                 payload::Midi,
                 payload::MidiSysex,
                 payload::Midi2>
        payload;

    template <typename S>
    void serialize(S& s) {
        s.value4b(time);
        s.value4b(flags);
        s.ext(payload, bitsery::ext::InPlaceVariant{});
    }
};

}  // namespace events
}  // namespace clap
