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

#include <bitsery/ext/std_variant.h>
#include <pluginterfaces/vst/ivstevents.h>

#include "base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * A wrapper around `DataEvent` for serialization purposes, as this event
 * contains a heap array.
 */
struct YaDataEvent {
    YaDataEvent();

    /**
     * Copy data from an existing `DataEvent`.
     */
    YaDataEvent(const Steinberg::Vst::DataEvent& event);

    /**
     * Reconstruct a `DataEvent` from this object.
     *
     * @note This object may contain pointers to data stored in this object, and
     *   must thus not outlive it.
     */
    Steinberg::Vst::DataEvent get() const;

    uint32 type;
    std::vector<uint8> buffer;

    template <typename S>
    void serialize(S& s) {
        s.value4b(type);
        s.container1b(buffer, 1 << 16);
    }
};

/**
 * A wrapper around `NoteExpressionTextEvent` for serialization purposes, as
 * this event contains a heap array.
 */
struct YaNoteExpressionTextEvent {
    YaNoteExpressionTextEvent();

    /**
     * Copy data from an existing `NoteExpressionTextEvent`.
     */
    YaNoteExpressionTextEvent(
        const Steinberg::Vst::NoteExpressionTextEvent& event);

    /**
     * Reconstruct a `NoteExpressionTextEvent` from this object.
     *
     * @note This object may contain pointers to data stored in this object, and
     *   must thus not outlive it.
     */
    Steinberg::Vst::NoteExpressionTextEvent get() const;

    Steinberg::Vst::NoteExpressionTypeID type_id;
    int32 note_id;

    std::u16string text;

    template <typename S>
    void serialize(S& s) {
        s.value4b(type_id);
        s.value4b(note_id);
        s.container2b(text, 1 << 16);
    }
};

/**
 * A wrapper around `ChordEvent` for serialization purposes, as this event
 * contains a heap array.
 */
struct YaChordEvent {
    YaChordEvent();

    /**
     * Copy data from an existing `ChordEvent`.
     */
    YaChordEvent(const Steinberg::Vst::ChordEvent& event);

    /**
     * Reconstruct a `ChordEvent` from this object.
     *
     * @note This object may contain pointers to data stored in this object, and
     *   must thus not outlive it.
     */
    Steinberg::Vst::ChordEvent get() const;

    int16 root;
    int16 bass_note;
    int16 mask;

    std::u16string text;

    template <typename S>
    void serialize(S& s) {
        s.value2b(root);
        s.value2b(bass_note);
        s.value2b(mask);
        s.container2b(text, 1 << 16);
    }
};

/**
 * A wrapper around `ScaleEvent` for serialization purposes, as this event
 * contains a heap array.
 */
struct YaScaleEvent {
    YaScaleEvent();

    /**
     * Copy data from an existing `ScaleEvent`.
     */
    YaScaleEvent(const Steinberg::Vst::ScaleEvent& event);

    /**
     * Reconstruct a `ScaleEvent` from this object.
     *
     * @note This object may contain pointers to data stored in this object, and
     *   must thus not outlive it.
     */
    Steinberg::Vst::ScaleEvent get() const;

    int16 root;
    int16 mask;

    std::u16string text;

    template <typename S>
    void serialize(S& s) {
        s.value2b(root);
        s.value2b(mask);
        s.container2b(text, 1 << 16);
    }
};

/**
 * A wrapper around `Event` for serialization purposes, as some event types
 * include heap pointers.
 */
struct YaEvent {
    YaEvent();

    /**
     * Copy data from an `Event`.
     */
    YaEvent(const Steinberg::Vst::Event& event);

    /**
     * Reconstruct an `Event` from this object.
     *
     * @note This object may contain pointers to data stored in this object, and
     *   must thus not outlive it.
     */
    Steinberg::Vst::Event get() const;

    // These fields directly reflect those from `Event`
    int32 bus_index;
    int32 sample_offset;
    Steinberg::Vst::TQuarterNotes ppq_position;
    uint16 flags;

    // `Event` stores an event type and a union, we'll encode both in a variant.
    // We can use simple types directly, and we need serializable wrappers
    // around move event types with heap pointers.
    std::variant<Steinberg::Vst::NoteOnEvent,
                 Steinberg::Vst::NoteOffEvent,
                 YaDataEvent,
                 Steinberg::Vst::PolyPressureEvent,
                 Steinberg::Vst::NoteExpressionValueEvent,
                 YaNoteExpressionTextEvent,
                 YaChordEvent,
                 YaScaleEvent,
                 Steinberg::Vst::LegacyMIDICCOutEvent>
        payload;

    template <typename S>
    void serialize(S& s) {
        s.value4b(bus_index);
        s.value4b(sample_offset);
        s.value8b(ppq_position);
        s.value2b(flags);
        s.ext(payload, bitsery::ext::StdVariant{});
    }
};

/**
 * Wraps around `IEventList` for serialization purposes. Used in
 * `YaProcessData`.
 */
class YaEventList : public Steinberg::Vst::IEventList {
   public:
    /**
     * Default constructor with an empty event list. The plugin can use this to
     * output data.
     */
    YaEventList();

    /**
     * Read data from an existing `IEventList` object.
     */
    YaEventList(Steinberg::Vst::IEventList& event_list);

    ~YaEventList();

    DECLARE_FUNKNOWN_METHODS

    /**
     * Return the number of events we store. Used in debug logs.
     */
    size_t num_events() const;

    /**
     * Write these events an output events queue on the `ProcessData` object
     * provided by the host.
     */
    void write_back_outputs(Steinberg::Vst::IEventList& output_events) const;

    // From `IEventList`
    virtual int32 PLUGIN_API getEventCount() override;
    virtual tresult PLUGIN_API
    getEvent(int32 index, Steinberg::Vst::Event& e /*out*/) override;
    virtual tresult PLUGIN_API
    addEvent(Steinberg::Vst::Event& e /*in*/) override;

    template <typename S>
    void serialize(S& s) {
        s.container(events, 1 << 16);
    }

   private:
    std::vector<YaEvent> events;

    /**
     * On the first `getEvent()` call we'll reconstruct these from `events` all
     * at once. These event objects may not outlive this event list.
     */
    std::vector<Steinberg::Vst::Event> reconstructed_events;
};

namespace Steinberg {
namespace Vst {
template <typename S>
void serialize(S& s, NoteOnEvent& event) {
    s.value2b(event.channel);
    s.value2b(event.pitch);
    s.value4b(event.tuning);
    s.value4b(event.velocity);
    s.value4b(event.length);
    s.value4b(event.noteId);
}

template <typename S>
void serialize(S& s, NoteOffEvent& event) {
    s.value2b(event.channel);
    s.value2b(event.pitch);
    s.value4b(event.velocity);
    s.value4b(event.noteId);
    s.value4b(event.tuning);
}

template <typename S>
void serialize(S& s, PolyPressureEvent& event) {
    s.value2b(event.channel);
    s.value2b(event.pitch);
    s.value4b(event.pressure);
    s.value4b(event.noteId);
}

template <typename S>
void serialize(S& s, NoteExpressionValueEvent& event) {
    s.value4b(event.typeId);
    s.value4b(event.noteId);
    s.value8b(event.value);
}

template <typename S>
void serialize(S& s, LegacyMIDICCOutEvent& event) {
    s.value1b(event.controlNumber);
    s.value1b(event.channel);
    s.value1b(event.value);
    s.value1b(event.value2);
}
}  // namespace Vst
}  // namespace Steinberg

#pragma GCC diagnostic pop
