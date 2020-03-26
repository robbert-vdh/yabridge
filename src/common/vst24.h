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

// This file contains important opcodes and structs missing from
// `vestige/aeffectx.h`

// Glanced from https://www.kvraudio.com/forum/viewtopic.php?p=2744675#p2744675.
// These opcodes are used to retrieve names and specific properties for a
// plugin's inputs and outputs, if the plugin supports this. The index parameter
// is used to specify the index of the channel being queried, and the plugin
// gets passed an empty struct to describe the input/output through the data
// parameter. Finally the plugin returns a string containing the input or output
// name.
constexpr int effGetInputProperties = 33;
constexpr int effGetOutputProperties = 34;

/**
 * The struct that's being passed through the data parameter during the
 * `effGetInputProperties` and `effGetOutputProperties` opcodes. Reverse
 * engineered by attaching gdb to Bitwig. The actual fields are missing but for
 * this application we don't need them.
 */
struct VstIOProperties {
    char data[128];
};
