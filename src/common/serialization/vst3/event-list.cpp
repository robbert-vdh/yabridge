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

#include "event-list.h"

YaDataEvent::YaDataEvent() {}

YaDataEvent::YaDataEvent(const Steinberg::Vst::DataEvent& event)
    : type(event.type), buffer(event.bytes, event.bytes + event.size) {}

Steinberg::Vst::DataEvent YaDataEvent::get() const {
    return Steinberg::Vst::DataEvent{.size = static_cast<uint32>(buffer.size()),
                                     .type = type,
                                     .bytes = buffer.data()};
}

YaNoteExpressionTextEvent::YaNoteExpressionTextEvent() {}

YaNoteExpressionTextEvent::YaNoteExpressionTextEvent(
    const Steinberg::Vst::NoteExpressionTextEvent& event)
    : type_id(event.typeId),
      note_id(event.noteId),
      text(tchar_pointer_to_u16string(event.text, event.textLen)) {}

Steinberg::Vst::NoteExpressionTextEvent YaNoteExpressionTextEvent::get() const {
    return Steinberg::Vst::NoteExpressionTextEvent{
        .typeId = type_id,
        .noteId = note_id,
        .textLen = static_cast<uint32>(text.size()),
        .text = u16string_to_tchar_pointer(text)};
}

YaChordEvent::YaChordEvent() {}

YaChordEvent::YaChordEvent(const Steinberg::Vst::ChordEvent& event)
    : root(event.root),
      bass_note(event.bassNote),
      text(tchar_pointer_to_u16string(event.text, event.textLen)) {}

Steinberg::Vst::ChordEvent YaChordEvent::get() const {
    return Steinberg::Vst::ChordEvent{
        .root = root,
        .bassNote = bass_note,
        .textLen = static_cast<uint16>(text.size()),
        .text = u16string_to_tchar_pointer(text)};
}

YaScaleEvent::YaScaleEvent() {}

YaScaleEvent::YaScaleEvent(const Steinberg::Vst::ScaleEvent& event)
    : root(event.root),
      text(tchar_pointer_to_u16string(event.text, event.textLen)) {}

Steinberg::Vst::ScaleEvent YaScaleEvent::get() const {
    return Steinberg::Vst::ScaleEvent{
        .root = root,
        .textLen = static_cast<uint16>(text.size()),
        .text = u16string_to_tchar_pointer(text)};
}
