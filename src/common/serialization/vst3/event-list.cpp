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

#include "event-list.h"

#include "../../utils.h"

YaDataEvent::YaDataEvent() noexcept {}

YaDataEvent::YaDataEvent(const Steinberg::Vst::DataEvent& event) noexcept
    : type(event.type), buffer(event.bytes, event.bytes + event.size) {}

Steinberg::Vst::DataEvent YaDataEvent::get() const noexcept {
    static_assert(sizeof(decltype(buffer)::value_type) == sizeof(uint8));

    return Steinberg::Vst::DataEvent{
        .size = static_cast<uint32>(buffer.size()),
        .type = type,
        .bytes = reinterpret_cast<const uint8*>(buffer.data())};
}

YaNoteExpressionTextEvent::YaNoteExpressionTextEvent() noexcept {}

YaNoteExpressionTextEvent::YaNoteExpressionTextEvent(
    const Steinberg::Vst::NoteExpressionTextEvent& event) noexcept
    : type_id(event.typeId),
      note_id(event.noteId),
      text(tchar_pointer_to_u16string(event.text, event.textLen)) {}

Steinberg::Vst::NoteExpressionTextEvent YaNoteExpressionTextEvent::get()
    const noexcept {
    return Steinberg::Vst::NoteExpressionTextEvent{
        .typeId = type_id,
        .noteId = note_id,
        .textLen = static_cast<uint32>(text.size()),
        .text = u16string_to_tchar_pointer(text)};
}

YaChordEvent::YaChordEvent() noexcept {}

YaChordEvent::YaChordEvent(const Steinberg::Vst::ChordEvent& event) noexcept
    : root(event.root),
      bass_note(event.bassNote),
      mask(event.mask),
      text(tchar_pointer_to_u16string(event.text, event.textLen)) {}

Steinberg::Vst::ChordEvent YaChordEvent::get() const noexcept {
    return Steinberg::Vst::ChordEvent{
        .root = root,
        .bassNote = bass_note,
        .mask = mask,
        .textLen = static_cast<uint16>(text.size()),
        .text = u16string_to_tchar_pointer(text)};
}

YaScaleEvent::YaScaleEvent() noexcept {}

YaScaleEvent::YaScaleEvent(const Steinberg::Vst::ScaleEvent& event) noexcept
    : root(event.root),
      mask(event.mask),
      text(tchar_pointer_to_u16string(event.text, event.textLen)) {}

Steinberg::Vst::ScaleEvent YaScaleEvent::get() const noexcept {
    return Steinberg::Vst::ScaleEvent{
        .root = root,
        .mask = mask,
        .textLen = static_cast<uint16>(text.size()),
        .text = u16string_to_tchar_pointer(text)};
}

YaEvent::YaEvent() noexcept {}

YaEvent::YaEvent(const Steinberg::Vst::Event& event) noexcept
    : bus_index(event.busIndex),
      sample_offset(event.sampleOffset),
      ppq_position(event.ppqPosition),
      flags(event.flags) {
    // Now we need the correct event type
    switch (event.type) {
        case Steinberg::Vst::Event::kNoteOnEvent:
            payload = event.noteOn;
            break;
        case Steinberg::Vst::Event::kNoteOffEvent:
            payload = event.noteOff;
            break;
        case Steinberg::Vst::Event::kDataEvent:
            payload = YaDataEvent(event.data);
            break;
        case Steinberg::Vst::Event::kPolyPressureEvent:
            payload = event.polyPressure;
            break;
        case Steinberg::Vst::Event::kNoteExpressionValueEvent:
            payload = event.noteExpressionValue;
            break;
        case Steinberg::Vst::Event::kNoteExpressionTextEvent:
            payload = YaNoteExpressionTextEvent(event.noteExpressionText);
            break;
        case Steinberg::Vst::Event::kChordEvent:
            payload = YaChordEvent(event.chord);
            break;
        case Steinberg::Vst::Event::kScaleEvent:
            payload = YaScaleEvent(event.scale);
            break;
        case Steinberg::Vst::Event::kLegacyMIDICCOutEvent:
            payload = event.midiCCOut;
            break;
        default:
            // XXX: When encountering something we don't know about, should we
            //      throw or silently ignore it? We can't properly log about
            //      this directly from here.
            break;
    }
}

Steinberg::Vst::Event YaEvent::get() const noexcept {
    // We of course can't fully initialize a field with an untagged union
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    Steinberg::Vst::Event event{.busIndex = bus_index,
                                .sampleOffset = sample_offset,
                                .ppqPosition = ppq_position,
                                .flags = flags};
#pragma GCC diagnostic pop
    std::visit(
        overload{
            [&](const Steinberg::Vst::NoteOnEvent& specific_event) {
                event.type = Steinberg::Vst::Event::kNoteOnEvent;
                event.noteOn = specific_event;
            },
            [&](const Steinberg::Vst::NoteOffEvent& specific_event) {
                event.type = Steinberg::Vst::Event::kNoteOffEvent;
                event.noteOff = specific_event;
            },
            [&](const YaDataEvent& specific_event) {
                event.type = Steinberg::Vst::Event::kDataEvent;
                event.data = specific_event.get();
            },
            [&](const Steinberg::Vst::PolyPressureEvent& specific_event) {
                event.type = Steinberg::Vst::Event::kPolyPressureEvent;
                event.polyPressure = specific_event;
            },
            [&](const Steinberg::Vst::NoteExpressionValueEvent&
                    specific_event) {
                event.type = Steinberg::Vst::Event::kNoteExpressionValueEvent;
                event.noteExpressionValue = specific_event;
            },
            [&](const YaNoteExpressionTextEvent& specific_event) {
                event.type = Steinberg::Vst::Event::kNoteExpressionTextEvent;
                event.noteExpressionText = specific_event.get();
            },
            [&](const YaChordEvent& specific_event) {
                event.type = Steinberg::Vst::Event::kChordEvent;
                event.chord = specific_event.get();
            },
            [&](const YaScaleEvent& specific_event) {
                event.type = Steinberg::Vst::Event::kScaleEvent;
                event.scale = specific_event.get();
            },
            [&](const Steinberg::Vst::LegacyMIDICCOutEvent& specific_event) {
                event.type = Steinberg::Vst::Event::kLegacyMIDICCOutEvent;
                event.midiCCOut = specific_event;
            }},
        payload);

    return event;
}

YaEventList::YaEventList() noexcept {
    FUNKNOWN_CTOR
}

void YaEventList::clear() noexcept {
    events_.clear();
}

void YaEventList::repopulate(Steinberg::Vst::IEventList& event_list) {
    // Copy over all events. Everything gets converted to `YaEvent`s. We sadly
    // can't construct these in place because we don't know the event type yet.
    events_.clear();
    events_.reserve(event_list.getEventCount());
    for (int i = 0; i < event_list.getEventCount(); i++) {
        // We're skipping the `kResultOk` assertions here
        Steinberg::Vst::Event event;
        event_list.getEvent(i, event);
        events_.emplace_back(event);
    }
}

YaEventList::~YaEventList() noexcept {FUNKNOWN_DTOR}

size_t YaEventList::num_events() const noexcept {
    return events_.size();
}

void YaEventList::write_back_outputs(
    Steinberg::Vst::IEventList& output_events) const {
    for (auto& event : events_) {
        Steinberg::Vst::Event reconstructed_event = event.get();
        output_events.addEvent(reconstructed_event);
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
IMPLEMENT_FUNKNOWN_METHODS(YaEventList,
                           Steinberg::Vst::IEventList,
                           Steinberg::Vst::IEventList::iid)
#pragma GCC diagnostic pop

int32 PLUGIN_API YaEventList::getEventCount() {
    return static_cast<int32>(events_.size());
}

tresult PLUGIN_API YaEventList::getEvent(int32 index,
                                         Steinberg::Vst::Event& e /*out*/) {
    if (index < 0 || index >= static_cast<int32>(events_.size())) {
        return Steinberg::kInvalidArgument;
    }

    // Reconstructing an event is cheap, but some events may contain pointers to
    // heap data stored within the `events` vector so this event will still have
    // the same lifetime as this class
    e = events_[index].get();

    return Steinberg::kResultOk;
}

tresult PLUGIN_API YaEventList::addEvent(Steinberg::Vst::Event& e /*in*/) {
    events_.push_back(e);

    return Steinberg::kResultOk;
}
