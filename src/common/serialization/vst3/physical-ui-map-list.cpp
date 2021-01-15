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

#include "physical-ui-map-list.h"

#include <cassert>

YaPhysicalUIMapList::YaPhysicalUIMapList() {}

YaPhysicalUIMapList::YaPhysicalUIMapList(
    const Steinberg::Vst::PhysicalUIMapList& list)
    : maps(list.map, list.map + list.count) {}

Steinberg::Vst::PhysicalUIMapList YaPhysicalUIMapList::get() {
    return Steinberg::Vst::PhysicalUIMapList{
        .count = static_cast<Steinberg::uint32>(maps.size()),
        .map = maps.data()};
}

void YaPhysicalUIMapList::write_back(
    Steinberg::Vst::PhysicalUIMapList& list) const {
    assert(list.count == maps.size());

    // Write the note expression IDs as updated by the plugin (if the plugin
    // updated them) back to the original list we've read from
    for (Steinberg::uint32 i = 0; i < list.count; i++) {
        list.map[i].noteExpressionTypeID = maps[i].noteExpressionTypeID;
    }
}
