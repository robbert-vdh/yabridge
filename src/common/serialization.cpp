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

#include "serialization.h"

DynamicVstEvents::DynamicVstEvents(const VstEvents& c_events)
    : events(c_events.numEvents) {
    // Copy from the C-style array into a vector for serialization
    for (int i = 0; i < c_events.numEvents; i++) {
        events[i] = *c_events.events[i];
    }
}

VstEvents& DynamicVstEvents::as_c_events() {
    // As explained in `vst_events_buffer`'s docstring we have to build the
    // `VstEvents` struct by hand on the heap since it's actually a dynamically
    // sized object

    // First we need to allocate enough memory for the entire object. The events
    // are stored as pointers to objects in the `events` vector that we sent
    // over the socket.
    static_assert(std::extent_v<decltype(VstEvents::events)> == 1);
    const size_t buffer_size =
        sizeof(VstEvents) + ((events.size() - 1) * sizeof(VstEvent*));
    vst_events_buffer.resize(buffer_size);

    // Now we can populate the VLA with pointers to the objects in the `events`
    // vector
    VstEvents* vst_events =
        reinterpret_cast<VstEvents*>(vst_events_buffer.data());
    vst_events->numEvents = events.size();
    std::transform(events.begin(), events.end(), vst_events->events,
                   [](VstEvent& event) -> VstEvent* { return &event; });

    return *vst_events;
}

DynamicSpeakerArrangement::DynamicSpeakerArrangement(
    const VstSpeakerArrangement& speaker_arrangement)
    : flags(speaker_arrangement.flags),
      speakers(speaker_arrangement.num_speakers) {
    using speaker_type =
        std::remove_extent_t<decltype(speaker_arrangement.speakers)>;
    static_assert(std::is_same_v<speaker_type, VstSpeaker>);

    // Copy from the C-style array into a vector for serialization
    speakers.assign(
        speaker_arrangement.speakers,
        speaker_arrangement.speakers +
            (speaker_arrangement.num_speakers * sizeof(speaker_type)));
}

VstSpeakerArrangement& DynamicSpeakerArrangement::as_c_speaker_arrangement() {
    // Just like in `DynamicVstEvents::as_c_events()`, we will use our buffer
    // vector to allocate enough heap space and then reconstruct the original
    // `VstSpeakerArrangement` object passed to the constructor.
    static_assert(std::extent_v<decltype(VstSpeakerArrangement::speakers)> ==
                  2);
    const size_t buffer_size = sizeof(VstSpeakerArrangement) +
                               ((speakers.size() - 2) * sizeof(VstSpeaker));
    speaker_arrangement_buffer.resize(buffer_size);

    // Now we'll just copy over the elements from our vector to the VLA in this
    // struct
    VstSpeakerArrangement* speaker_arrangement =
        reinterpret_cast<VstSpeakerArrangement*>(
            speaker_arrangement_buffer.data());
    speaker_arrangement->flags = flags;
    speaker_arrangement->num_speakers = speakers.size();
    std::copy(speakers.begin(), speakers.end(), speaker_arrangement->speakers);

    return *speaker_arrangement;
}
