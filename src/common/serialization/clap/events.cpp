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

#include "events.h"

namespace clap {
namespace events {

std::optional<Event> Event::parse(const clap_event_header_t& generic_event) {
    std::optional<decltype(Event::payload)> payload;
    if (generic_event.space_id == CLAP_CORE_EVENT_SPACE_ID) {
        switch (generic_event.type) {
            case CLAP_EVENT_NOTE_ON: {
                const auto& event =
                    reinterpret_cast<const clap_event_note_t&>(generic_event);
                payload = payload::Note{
                    .event_type = payload::NoteEventType::On,
                    .note_id = event.note_id,
                    .port_index = event.port_index,
                    .channel = event.channel,
                    .key = event.key,
                    .velocity = event.velocity,
                };
            } break;
            case CLAP_EVENT_NOTE_OFF: {
                const auto& event =
                    reinterpret_cast<const clap_event_note_t&>(generic_event);
                payload = payload::Note{
                    .event_type = payload::NoteEventType::Off,
                    .note_id = event.note_id,
                    .port_index = event.port_index,
                    .channel = event.channel,
                    .key = event.key,
                    .velocity = event.velocity,
                };
            } break;
            case CLAP_EVENT_NOTE_CHOKE: {
                const auto& event =
                    reinterpret_cast<const clap_event_note_t&>(generic_event);
                payload = payload::Note{
                    .event_type = payload::NoteEventType::Choke,
                    .note_id = event.note_id,
                    .port_index = event.port_index,
                    .channel = event.channel,
                    .key = event.key,
                    .velocity = event.velocity,
                };
            } break;
            case CLAP_EVENT_NOTE_END: {
                const auto& event =
                    reinterpret_cast<const clap_event_note_t&>(generic_event);
                payload = payload::Note{
                    .event_type = payload::NoteEventType::End,
                    .note_id = event.note_id,
                    .port_index = event.port_index,
                    .channel = event.channel,
                    .key = event.key,
                    .velocity = event.velocity,
                };
            } break;
            case CLAP_EVENT_NOTE_EXPRESSION: {
                const auto& event =
                    reinterpret_cast<const clap_event_note_expression_t&>(
                        generic_event);
                payload = payload::NoteExpression{
                    .expression_id = event.expression_id,
                    .note_id = event.note_id,
                    .port_index = event.port_index,
                    .channel = event.channel,
                    .key = event.key,
                    .value = event.value,
                };
            } break;
            case CLAP_EVENT_PARAM_VALUE: {
                const auto& event =
                    reinterpret_cast<const clap_event_param_value_t&>(
                        generic_event);
                payload = payload::ParamValue{
                    .param_id = event.param_id,
                    .cookie = static_cast<native_size_t>(
                        reinterpret_cast<size_t>(event.cookie)),
                    .note_id = event.note_id,
                    .port_index = event.port_index,
                    .channel = event.channel,
                    .key = event.key,
                    .value = event.value,
                };
            } break;
            case CLAP_EVENT_PARAM_MOD: {
                const auto& event =
                    reinterpret_cast<const clap_event_param_mod_t&>(
                        generic_event);
                payload = payload::ParamMod{
                    .param_id = event.param_id,
                    .cookie = static_cast<native_size_t>(
                        reinterpret_cast<size_t>(event.cookie)),
                    .note_id = event.note_id,
                    .port_index = event.port_index,
                    .channel = event.channel,
                    .key = event.key,
                    .amount = event.amount,
                };
            } break;
            case CLAP_EVENT_PARAM_GESTURE_BEGIN: {
                const auto& event =
                    reinterpret_cast<const clap_event_param_gesture_t&>(
                        generic_event);
                payload = payload::ParamGesture{
                    .gesture_type = payload::ParamGestureType::Begin,
                    .param_id = event.param_id,
                };
            } break;
            case CLAP_EVENT_PARAM_GESTURE_END: {
                const auto& event =
                    reinterpret_cast<const clap_event_param_gesture_t&>(
                        generic_event);
                payload = payload::ParamGesture{
                    .gesture_type = payload::ParamGestureType::End,
                    .param_id = event.param_id,
                };
            } break;
            case CLAP_EVENT_TRANSPORT: {
                const auto& event =
                    reinterpret_cast<const clap_event_transport_t&>(
                        generic_event);
                payload = payload::Transport{
                    .flags = event.flags,
                    .song_pos_beats = event.song_pos_beats,
                    .song_pos_seconds = event.song_pos_seconds,
                    .tempo = event.tempo,
                    .tempo_inc = event.tempo_inc,
                    .loop_start_beats = event.loop_start_beats,
                    .loop_end_beats = event.loop_end_beats,
                    .loop_start_seconds = event.loop_start_seconds,
                    .loop_end_seconds = event.loop_end_seconds,
                    .bar_start = event.bar_start,
                    .bar_number = event.bar_number,
                    .tsig_num = event.tsig_num,
                    .tsig_denom = event.tsig_denom,
                };
            } break;
            case CLAP_EVENT_MIDI: {
                const auto& event =
                    reinterpret_cast<const clap_event_midi_t&>(generic_event);
                payload = payload::Midi{
                    .port_index = event.port_index,
                    .data{event.data[0], event.data[1], event.data[2]},
                };
            } break;
            case CLAP_EVENT_MIDI_SYSEX: {
                const auto& event =
                    reinterpret_cast<const clap_event_midi_sysex_t&>(
                        generic_event);
                assert(event.buffer);
                payload = payload::MidiSysex{
                    .port_index = event.port_index,
                    .buffer =
                        std::string(reinterpret_cast<const char*>(event.buffer),
                                    event.size),
                };
            } break;
            case CLAP_EVENT_MIDI2: {
                const auto& event =
                    reinterpret_cast<const clap_event_midi2_t&>(generic_event);
                payload = payload::Midi2{
                    .port_index = event.port_index,
                    .data{event.data[0], event.data[1], event.data[2],
                          event.data[3]},
                };
            } break;
        }
    }

    if (payload) {
        return Event{.time = generic_event.time,
                     .flags = generic_event.flags,
                     .payload = std::move(*payload)};
    } else {
        return std::nullopt;
    }
}

}  // namespace events
}  // namespace clap
