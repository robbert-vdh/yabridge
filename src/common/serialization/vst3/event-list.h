// yabridge: a Wine VST bridge
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

#include <llvm/small-vector.h>
#include <pluginterfaces/vst/ivstevents.h>

#include "../../bitsery/ext/in-place-variant.h"
#include "../../bitsery/traits/small-vector.h"
#include "base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * A wrapper around `DataEvent` for serialization purposes, as this event
 * contains a heap array. This would presumably be used for SysEx.
 */
struct YaDataEvent {
    YaDataEvent() noexcept;

    /**
     * Copy data from an existing `DataEvent`.
     */
    YaDataEvent(const Steinberg::Vst::DataEvent& event) noexcept;

    /**
     * Reconstruct a `DataEvent` from this object.
     *
     * @note This object may contain pointers to data stored in this object, and
     *   must thus not outlive it.
     */
    Steinberg::Vst::DataEvent get() const noexcept;

    uint32 type;

    /**
     * We'll just abuse the standard library's Small String Optimization to
     * avoid allocations for small messages, just like we do for VST2 SysEx
     * events.
     */
    std::string buffer;

    template <typename S>
    void serialize(S& s) {
        s.value4b(type);
        s.text1b(buffer, 1 << 16);
    }
};

/**
 * A wrapper around `NoteExpressionTextEvent` for serialization purposes, as
 * this event contains a heap array.
 */
struct YaNoteExpressionTextEvent {
    YaNoteExpressionTextEvent() noexcept;

    /**
     * Copy data from an existing `NoteExpressionTextEvent`.
     */
    YaNoteExpressionTextEvent(
        const Steinberg::Vst::NoteExpressionTextEvent& event) noexcept;

    /**
     * Reconstruct a `NoteExpressionTextEvent` from this object.
     *
     * @note This object may contain pointers to data stored in this object, and
     *   must thus not outlive it.
     */
    Steinberg::Vst::NoteExpressionTextEvent get() const noexcept;

    Steinberg::Vst::NoteExpressionTypeID type_id;
    int32 note_id;

    std::u16string text;

    template <typename S>
    void serialize(S& s) {
        s.value4b(type_id);
        s.value4b(note_id);
        s.text2b(text, std::extent_v<Steinberg::Vst::String128>);
    }
};

/**
 * A wrapper around `ChordEvent` for serialization purposes, as this event
 * contains a heap array.
 */
struct YaChordEvent {
    YaChordEvent() noexcept;

    /**
     * Copy data from an existing `ChordEvent`.
     */
    YaChordEvent(const Steinberg::Vst::ChordEvent& event) noexcept;

    /**
     * Reconstruct a `ChordEvent` from this object.
     *
     * @note This object may contain pointers to data stored in this object, and
     *   must thus not outlive it.
     */
    Steinberg::Vst::ChordEvent get() const noexcept;

    int16 root;
    int16 bass_note;
    int16 mask;

    std::u16string text;

    template <typename S>
    void serialize(S& s) {
        s.value2b(root);
        s.value2b(bass_note);
        s.value2b(mask);
        s.text2b(text, std::extent_v<Steinberg::Vst::String128>);
    }
};

/**
 * A wrapper around `ScaleEvent` for serialization purposes, as this event
 * contains a heap array.
 */
struct YaScaleEvent {
    YaScaleEvent() noexcept;

    /**
     * Copy data from an existing `ScaleEvent`.
     */
    YaScaleEvent(const Steinberg::Vst::ScaleEvent& event) noexcept;

    /**
     * Reconstruct a `ScaleEvent` from this object.
     *
     * @note This object may contain pointers to data stored in this object, and
     *   must thus not outlive it.
     */
    Steinberg::Vst::ScaleEvent get() const noexcept;

    int16 root;
    int16 mask;

    std::u16string text;

    template <typename S>
    void serialize(S& s) {
        s.value2b(root);
        s.value2b(mask);
        s.text2b(text, std::extent_v<Steinberg::Vst::String128>);
    }
};

/**
 * A wrapper around `Event` for serialization purposes, as some event types
 * include heap pointers.
 */
struct alignas(16) YaEvent {
    YaEvent() noexcept;

    /**
     * Copy data from an `Event`.
     */
    YaEvent(const Steinberg::Vst::Event& event) noexcept;

    /**
     * Reconstruct an `Event` from this object.
     *
     * @note This object may contain pointers to data stored in this object, and
     *   must thus not outlive it.
     */
    Steinberg::Vst::Event get() const noexcept;

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
        s.ext(payload, bitsery::ext::InPlaceVariant{});
    }
};

/**
 * Wraps around `IEventList` for serialization purposes. Used in
 * `YaProcessData`.
 */
class YaEventList : public Steinberg::Vst::IEventList {
   public:
    /**
     * We only provide a default constructor here, because we need to fill the
     * existing object with new events every processing cycle to avoid
     * reallocating a new object every time.
     */
    YaEventList() noexcept;

    /**
     * Remove all events. Used when a null pointer gets passed to the input
     * events field, and so the plugin can output its own events if the host
     * supports this.
     */
    void clear() noexcept;

    /**
     * Read data from an `IEventList` object into this existing object.
     */
    void repopulate(Steinberg::Vst::IEventList& event_list);

    ~YaEventList() noexcept;

    DECLARE_FUNKNOWN_METHODS

    /**
     * Return the number of events we store. Used in debug logs.
     */
    size_t num_events() const noexcept;

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
        s.container(events_, 1 << 16);
    }

   private:
    llvm::SmallVector<YaEvent, 64> events_;
};

#pragma GCC diagnostic pop

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
