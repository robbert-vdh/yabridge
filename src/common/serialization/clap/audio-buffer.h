// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2023 Robbert van der Helm
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more destates.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <clap/audio-buffer.h>

// Serialization messages for `clap/audio-buffer.h`

namespace clap {
namespace audio_buffer {

/**
 * Metadata used to encode whether an audio port/buffer carries 32-=bit or
 * 64-bit audio data. This needs to be stored separately because CLAP uses
 * whether or not one of the two pointers is null to indicate the type of data
 * stored in the audio buffer.
 */
enum class AudioBufferType : uint8_t {
    Float32,
    Double64,
};

}  // namespace audio_buffer
}  // namespace clap

template <typename S>
void serialize(S& s, clap_audio_buffer_t& buffer) {
    // These need to be set later using the shared memory object and depending
    // on the `AudioBufferType`. We'll zero them out so default initialized
    // objects created by bitsery won't contain uninitialized memory.
    buffer.data32 = nullptr;
    buffer.data64 = nullptr;
    s.value4b(buffer.channel_count);
    s.value4b(buffer.latency);
    s.value8b(buffer.constant_mask);
}
