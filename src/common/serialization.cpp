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
    vst_events.numEvents = events.size();

    // Populate the vst_events struct with data from the vector. This will
    // overflow past the defined length of `vst_events.events` because it's
    // actually a VLA. This is why I put some padding at the end of this struct.
    std::transform(events.begin(), events.end(), &vst_events.events[0],
                   [](VstEvent& event) -> VstEvent* { return &event; });

    return vst_events;
}
