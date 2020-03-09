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

#include <iostream>

DynamicVstEvents::DynamicVstEvents(const VstEvents& c_events)
    : events(c_events.numEvents) {
    // Copy from the C-style array into a vector for serialization
    for (int i = 0; i < c_events.numEvents; i++) {
        events[i] = c_events.events[0][i];
    }
}

VstEvents& DynamicVstEvents::as_c_events() {
    // Populate the vst_events struct with data from the vector
    vst_events.numEvents = events.size();
    vst_events.events[0] = events.data();

    return vst_events;
}
