// yabridge: a Wine plugin bridge
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

#include <optional>
#include <string>
#include <vector>

#include <clap/ext/audio-ports.h>

#include "../../../bitsery/ext/in-place-optional.h"
#include "../../common.h"

// Serialization messages for `clap/ext/audio-ports.h`

namespace clap {
namespace ext {
namespace audio_ports {

/**
 * An enum representing the value of `clap_audio_port_info::port_type`. We can't
 * serialize the string directly as we still need to write a pointer to it with
 * static lifetime to the host's info struct.
 */
enum class AudioPortType : uint32_t {
    /** A null pointer or unrecognized value. */
    Unknown,
    /** `CLAP_PORT_MONO` */
    Mono,
    /** `CLAP_PORT_STEREO` */
    Stereo,

    // There are also special values for CV, surround, and ambisonics, but those
    // are part of draft extensions
};

/**
 * Convert a `clap_audio_port_info::port_type` string into our port type enum.
 */
AudioPortType parse_audio_port_type(const char* port_type);

/**
 * Convert an `AudioPortType` to a static string pointer that can be used in the
 * `clap_audio_port_info` struct. This is a null pointer if the port type was
 * unknown or unspecified.
 */
const char* audio_port_type_to_string(AudioPortType port_type);

/**
 * A serializable version of `clap_audio_port_info` that owns all of the data it
 * references.
 */
struct AudioPortInfo {
    /**
     * Parse a native `clap_audio_port_info` struct so it can be serialized and
     * sent to the Wine plugin host.
     */
    AudioPortInfo(const clap_audio_port_info_t& original);

    /**
     * Default constructor for bitsery.
     */
    AudioPortInfo() {}

    /**
     * Write the stored information to a host provided info struct.
     */
    void reconstruct(clap_audio_port_info_t& port_info) const;

    clap_id id;
    std::string name;
    uint32_t flags;
    uint32_t channel_count;
    AudioPortType port_type;
    clap_id in_place_pair;

    template <typename S>
    void serialize(S& s) {
        s.value4b(id);
        s.text1b(name, 4096);
        s.value4b(flags);
        s.value4b(channel_count);
        s.value4b(port_type);
        s.value4b(in_place_pair);
    }
};

namespace plugin {

/**
 * Message struct for `clap_plugin_audio_ports::count()`.
 */
struct Count {
    using Response = PrimitiveResponse<uint32_t>;

    native_size_t instance_id;
    bool is_input;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value1b(is_input);
    }
};

/**
 * The response to the `clap::ext::audio_ports::plugin::Get` message defined
 * below.
 */
struct GetResponse {
    std::optional<AudioPortInfo> result;

    template <typename S>
    void serialize(S& s) {
        s.ext(result, bitsery::ext::InPlaceOptional(),
              [](S& s, auto& v) { s.object(v); });
    }
};

/**
 * Message struct for `clap_plugin_audio_ports::get()`.
 */
struct Get {
    using Response = GetResponse;

    native_size_t instance_id;
    uint32_t index;
    bool is_input;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value4b(index);
        s.value1b(is_input);
    }
};

}  // namespace plugin

namespace host {

/**
 * Message struct for `clap_host_audio_ports::is_rescan_flag_supported()`.
 */
struct IsRescanFlagSupported {
    using Response = PrimitiveResponse<bool>;

    native_size_t owner_instance_id;
    uint32_t flag;

    template <typename S>
    void serialize(S& s) {
        s.value8b(owner_instance_id);
        s.value4b(flag);
    }
};

/**
 * Message struct for `clap_host_audio_ports::rescan()`.
 */
struct Rescan {
    using Response = Ack;

    native_size_t owner_instance_id;
    uint32_t flags;

    template <typename S>
    void serialize(S& s) {
        s.value8b(owner_instance_id);
        s.value4b(flags);
    }
};

}  // namespace host

}  // namespace audio_ports
}  // namespace ext
}  // namespace clap
