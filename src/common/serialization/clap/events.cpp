// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2023 Robbert van der Helm
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

#include "../../utils.h"

namespace clap {
namespace events {

std::optional<Event> Event::parse(const clap_event_header_t& generic_event) {
    std::optional<decltype(Event::payload)> payload;
    if (generic_event.space_id == CLAP_CORE_EVENT_SPACE_ID) {
        switch (generic_event.type) {
            case CLAP_EVENT_NOTE_ON:
            case CLAP_EVENT_NOTE_OFF:
            case CLAP_EVENT_NOTE_CHOKE:
            case CLAP_EVENT_NOTE_END: {
                const auto& event =
                    reinterpret_cast<const clap_event_note_t&>(generic_event);

                // The original event type can be restored from the header
                payload = payload::Note{.event = event};
            } break;
            case CLAP_EVENT_NOTE_EXPRESSION: {
                const auto& event =
                    reinterpret_cast<const clap_event_note_expression_t&>(
                        generic_event);
                payload = payload::NoteExpression{.event = event};
            } break;
            case CLAP_EVENT_PARAM_VALUE: {
                const auto& event =
                    reinterpret_cast<const clap_event_param_value_t&>(
                        generic_event);
                payload = payload::ParamValue{.event = event};
            } break;
            case CLAP_EVENT_PARAM_MOD: {
                const auto& event =
                    reinterpret_cast<const clap_event_param_mod_t&>(
                        generic_event);
                payload = payload::ParamMod{.event = event};
            } break;
            case CLAP_EVENT_PARAM_GESTURE_BEGIN:
            case CLAP_EVENT_PARAM_GESTURE_END: {
                const auto& event =
                    reinterpret_cast<const clap_event_param_gesture_t&>(
                        generic_event);
                payload = payload::ParamGesture{.event = event};
            } break;
            case CLAP_EVENT_TRANSPORT: {
                const auto& event =
                    reinterpret_cast<const clap_event_transport_t&>(
                        generic_event);
                payload = payload::Transport{.event = event};
            } break;
            case CLAP_EVENT_MIDI: {
                const auto& event =
                    reinterpret_cast<const clap_event_midi_t&>(generic_event);
                payload = payload::Midi{.event = event};
            } break;
            case CLAP_EVENT_MIDI_SYSEX: {
                const auto& event =
                    reinterpret_cast<const clap_event_midi_sysex_t&>(
                        generic_event);

                assert(event.buffer);
                const auto sysex_payload = payload::MidiSysex{
                    .event =
                        clap_event_midi_sysex_t{
                            .header = event.header,
                            .port_index = event.port_index,
                            // The buffer and size fields will be restored
                            // during the `get_header()` call. Nulling the
                            // pointer and zeroing the size should make
                            // incorrect usage much easier to spot than leaving
                            // them dangling.
                            .buffer = nullptr,
                            .size = 0},
                    .buffer =
                        std::string(reinterpret_cast<const char*>(event.buffer),
                                    event.size)};
            } break;
            case CLAP_EVENT_MIDI2: {
                const auto& event =
                    reinterpret_cast<const clap_event_midi2_t&>(generic_event);
                payload = payload::Midi2{.event = event};
            } break;
        }
    }

    if (payload) {
        return Event{.payload = std::move(*payload)};
    } else {
        return std::nullopt;
    }
}

const clap_event_header_t* Event::get() const {
    return std::visit(
        overload{[](payload::MidiSysex& event) -> const clap_event_header_t* {
                     // These events contain heap data pointers. We store this
                     // data using an `std::string` alongside the event struct,
                     // but we can only set the pointer here just before
                     // returning the event in case it was moved inbetween
                     // deserialization and this function being called.
                     event.event.buffer =
                         reinterpret_cast<const uint8_t*>(event.buffer.data());
                     event.event.size = event.buffer.size();

                     return &event.event.header;
                 },
                 [](const auto& event) -> const clap_event_header_t* {
                     return &event.event.header;
                 }},
        payload);
}

EventList::EventList() noexcept {}

void EventList::repopulate(const clap_input_events_t& in_events) {
    events_.clear();

    const uint32_t num_events = in_events.size(&in_events);
    for (uint32_t i = 0; i < num_events; i++) {
        const clap_event_header_t* event = in_events.get(&in_events, i);
        assert(event);

        if (std::optional<Event> parsed_event = Event::parse(*event);
            parsed_event) {
            events_.emplace_back(std::move(*parsed_event));
        }
    }
}

void EventList::clear() noexcept {
    events_.clear();
}

void EventList::write_back_outputs(
    const clap_output_events_t& out_events) const {
    for (const auto& event : events_) {
        // We'll ignore the result here, we can't handle it anyways and maybe
        // some hosts will return `false` for events they don't recognize
        // instead of only when out of memory
        out_events.try_push(&out_events, event.get());
    }
}

const clap_input_events_t* EventList::input_events() {
    input_events_vtable_.ctx = this;
    input_events_vtable_.size = in_size;
    input_events_vtable_.get = in_get;

    return &input_events_vtable_;
}

const clap_output_events_t* EventList::output_events() {
    output_events_vtable_.ctx = this;
    output_events_vtable_.try_push = out_try_push;

    return &output_events_vtable_;
}

uint32_t CLAP_ABI EventList::in_size(const struct clap_input_events* list) {
    assert(list && list->ctx);
    auto self = static_cast<const EventList*>(list->ctx);

    return self->events_.size();
}

const clap_event_header_t* CLAP_ABI
EventList::in_get(const struct clap_input_events* list, uint32_t index) {
    assert(list && list->ctx);
    auto self = static_cast<const EventList*>(list->ctx);

    if (index < self->events_.size()) {
        return self->events_[index].get();
    } else {
        return nullptr;
    }
}

bool CLAP_ABI EventList::out_try_push(const struct clap_output_events* list,
                                      const clap_event_header_t* event) {
    assert(list && list->ctx && event);
    auto self = static_cast<EventList*>(list->ctx);

    if (std::optional<Event> parsed_event = Event::parse(*event);
        parsed_event) {
        self->events_.emplace_back(std::move(*parsed_event));
    }

    // We'll pretend we accepted the event even if we don't recognize it
    return true;
}

}  // namespace events
}  // namespace clap
