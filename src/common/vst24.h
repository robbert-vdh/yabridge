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

#pragma once

// This file contains important opcodes and structs missing from
// `vestige/aeffectx.h`

/**
 * Glanced from https://www.kvraudio.com/forum/viewtopic.php?p=2744675#p2744675.
 * These opcodes are used to retrieve names and specific properties for a
 * plugin's inputs and outputs, if the plugin supports this. The index parameter
 * is used to specify the index of the channel being queried, and the plugin
 * gets passed an empty struct to describe the input/output through the data
 * parameter. Finally the plugin returns a string containing the input or output
 * name.
 */
[[maybe_unused]] constexpr int effGetInputProperties = 33;
[[maybe_unused]] constexpr int effGetOutputProperties = 34;

/**
 * Found on
 * https://github.com/falkTX/Carla/blob/07e876a743c5e15c358be170af2e523eadc7dbfa/source/utils/CarlaVstUtils.hpp#L75
 * Used to assign names to MIDI keys, for some reason uses the `VstMidiKeyName`
 * struct defined below rather than a simple string.
 */
[[maybe_unused]] constexpr int effGetMidiKeyName = 66;

/**
 * Events used to tell a plugin to use a specific speaker arrangement (is this
 * used outside of things like Dolby Atmos?), or to query its preferred speaker
 * arrangement. Found on the same list as above.
 */
[[maybe_unused]] constexpr int effSetSpeakerArrangement = 42;
[[maybe_unused]] constexpr int effGetSpeakerArrangement = 69;

/**
 * Used by REAPER for some VST2.4 extensions. Most of the arguments passed to
 * this will be able to be handled automatically by our `DefaultDataConverter`.
 * We need one special case because for when they're now using the `data`
 * argument with a non-pointer value. Found on the same list as above.
 */
[[maybe_unused]] constexpr int effVendorSpecific = 50;

/**
 * Set a parameter based on a string, kind of the inverse of the inverse of
 * `effGetParamDisplay()` and an alternative to `setParameter()`. Also found in
 * the list in Carla's repo. It's used in this way in JUCE here:
 * https://github.com/juce-framework/JUCE/blob/b34e798f392179caf9c67dce273398fa03352067/modules/juce_audio_plugin_client/VST/juce_VST_Wrapper.cpp#L927
 */
[[maybe_unused]] constexpr int effString2Parameter = 27;

/**
 * Used by hosts to query the length of reverb tails (equivalent to
 * `IAudioProcessor::getTailSamples`). Found on the same list as above.
 */
[[maybe_unused]] constexpr int effGetTailSize = 52;

/**
 * The struct that's being passed through the data parameter during the
 * `effGetInputProperties` and `effGetOutputProperties` opcodes. Reverse
 * engineered by attaching gdb to Bitwig. The actual fields are missing but for
 * this application we don't need them.
 */
struct VstIOProperties {
    char data[128];
};

/**
 * The struct that's passed during `effGetMidiKeyName`. Will be used to write
 * the name of a key to (i.e. the name of a sample for drum machines). Again,
 * not sure about the exact contents of this struct, but at least the size is
 * right!
 */
struct VstMidiKeyName {
    char data[80];
};

/**
 * Contains information about a speaker, used during
 * `eff{Get,Set}SpeakerArrangement`.
 */
struct VstSpeaker {
    char data[112];
};

/**
 * Contains information about a speaker setup, either for input or output. Used
 * during `eff{Get,Set}SpeakerArrangement`. Reverse engineered from Renoise by
 * attaching gdb and dumping both the `value` and `data` pointers when the host
 * calls opcode 42.
 *
 * Use the `DynamicSpeakerArrangement` class to serialize and construct these
 * objects.
 */
struct VstSpeakerArrangement {
    int flags;
    int num_speakers;
    /**
     * Variable length array of speakers. Similar to how `VstEvents` works, but
     * with an array of objects instead of an array of pointers to objects.
     */
    VstSpeaker speakers[2];
};
