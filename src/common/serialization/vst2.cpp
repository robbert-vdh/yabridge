// yabridge: a Wine VST bridge
// Copyright (C) 2020-2021 Robbert van der Helm
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

#include "vst2.h"

AEffect& update_aeffect(AEffect& plugin,
                        const AEffect& updated_plugin) noexcept {
    plugin.magic = updated_plugin.magic;
    plugin.numPrograms = updated_plugin.numPrograms;
    plugin.numParams = updated_plugin.numParams;
    plugin.numInputs = updated_plugin.numInputs;
    plugin.numOutputs = updated_plugin.numOutputs;
    plugin.flags = updated_plugin.flags;
    plugin.initialDelay = updated_plugin.initialDelay;
    plugin.empty3a = updated_plugin.empty3a;
    plugin.empty3b = updated_plugin.empty3b;
    plugin.unkown_float = updated_plugin.unkown_float;
    plugin.uniqueID = updated_plugin.uniqueID;
    plugin.version = updated_plugin.version;

    return plugin;
}

DynamicVstEvents::DynamicVstEvents() noexcept {}

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
    // over the socket. Our definition of `VstEvents` contains a single
    // `VstEvent`, so our buffer needs to be large enough to store that plus the
    // number of events minus one pointers.
    static_assert(std::extent_v<decltype(VstEvents::events)> == 1);
    const size_t buffer_size =
        sizeof(VstEvents) +
        ((events.size() - 1) *
         sizeof(VstEvent*));  // NOLINT(bugprone-sizeof-expression)
    vst_events_buffer.resize(buffer_size);

    // Now we can populate the VLA with pointers to the objects in the `events`
    // vector
    VstEvents* vst_events =
        reinterpret_cast<VstEvents*>(vst_events_buffer.data());
    vst_events->numEvents = static_cast<int>(events.size());
    std::transform(events.begin(), events.end(), vst_events->events,
                   [](VstEvent& event) -> VstEvent* { return &event; });

    return *vst_events;
}

DynamicSpeakerArrangement::DynamicSpeakerArrangement() noexcept {}

DynamicSpeakerArrangement::DynamicSpeakerArrangement(
    const VstSpeakerArrangement& speaker_arrangement)
    : flags(speaker_arrangement.flags),
      speakers(speaker_arrangement.num_speakers) {
    // Copy from the C-style array into a vector for serialization
    speakers.assign(
        &speaker_arrangement.speakers[0],
        &speaker_arrangement.speakers[speaker_arrangement.num_speakers]);
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
    speaker_arrangement->num_speakers = static_cast<int>(speakers.size());
    std::copy(speakers.begin(), speakers.end(), speaker_arrangement->speakers);

    return *speaker_arrangement;
}

std::vector<uint8_t>& DynamicSpeakerArrangement::as_raw_data() {
    // This will populate the buffer for us with the struct data
    as_c_speaker_arrangement();

    return speaker_arrangement_buffer;
}
