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
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <clap/ext/audio-ports-config.h>

#include "../../../bitsery/ext/in-place-optional.h"
#include "../../common.h"
#include "audio-ports.h"

// Serialization messages for `clap/ext/audio-ports-config.h`

namespace clap {
namespace ext {
namespace audio_ports_config {

/**
 * A serializable version of `clap_audio_ports_config_t` that owns all of the
 * data it references.
 */
struct AudioPortsConfig {
    /**
     * Parse a native `clap_audio_ports_config` struct so it can be serialized
     * and sent to the Wine plugin host.
     */
    AudioPortsConfig(const clap_audio_ports_config_t& original);

    /**
     * Default constructor for bitsery.
     */
    AudioPortsConfig() {}

    /**
     * Write the stored configuration to a host provided struct.
     */
    void reconstruct(clap_audio_ports_config_t& config) const;

    clap_id id;
    std::string name;
    uint32_t input_port_count;
    uint32_t output_port_count;

    bool has_main_input;
    uint32_t main_input_channel_count;
    clap::ext::audio_ports::AudioPortType main_input_port_type;

    bool has_main_output;
    uint32_t main_output_channel_count;
    clap::ext::audio_ports::AudioPortType main_output_port_type;

    template <typename S>
    void serialize(S& s) {
        s.value4b(id);
        s.text1b(name, 4096);
        s.value4b(input_port_count);
        s.value4b(output_port_count);
        s.value1b(has_main_input);
        s.value4b(main_input_channel_count);
        s.value4b(main_input_port_type);
        s.value1b(has_main_output);
        s.value4b(main_output_channel_count);
        s.value4b(main_output_port_type);
    }
};

namespace plugin {

/**
 * Message struct for `clap_plugin_audio_ports_config::count()`.
 */
struct Count {
    using Response = PrimitiveResponse<uint32_t>;

    native_size_t instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
    }
};

/**
 * The response to the `clap::ext::audio_ports_config::plugin::Get` message
 * defined below.
 */
struct GetResponse {
    std::optional<AudioPortsConfig> result;

    template <typename S>
    void serialize(S& s) {
        s.ext(result, bitsery::ext::InPlaceOptional());
    }
};

/**
 * Message struct for `clap_plugin_audio_ports_config::get()`.
 */
struct Get {
    using Response = GetResponse;

    native_size_t instance_id;
    uint32_t index;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value4b(index);
    }
};

/**
 * Message struct for `clap_plugin_audio_ports_config::select()`.
 */
struct Select {
    using Response = PrimitiveResponse<bool>;

    native_size_t instance_id;
    clap_id config_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value4b(config_id);
    }
};

}  // namespace plugin

namespace host {

/**
 * Message struct for `clap_host_audio_ports_config::rescan()`.
 */
struct Rescan {
    using Response = Ack;

    native_size_t owner_instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(owner_instance_id);
    }
};

}  // namespace host

}  // namespace audio_ports_config
}  // namespace ext
}  // namespace clap
