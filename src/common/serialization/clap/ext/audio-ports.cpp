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

#include "audio-ports.h"

namespace clap {
namespace ext {
namespace audio_ports {

AudioPortType parse_port_type(const char* port_type) {
    if (!port_type) {
        return AudioPortType::Unknown;
    }

    if (strcmp(port_type, CLAP_PORT_MONO) == 0) {
        return AudioPortType::Mono;
    } else if (strcmp(port_type, CLAP_PORT_STEREO) == 0) {
        return AudioPortType::Stereo;
    } else {
        return AudioPortType::Unknown;
    }
}

const char* audio_port_type_to_string(AudioPortType port_type) {
    switch (port_type) {
        case AudioPortType::Mono:
            return CLAP_PORT_MONO;
            break;
        case AudioPortType::Stereo:
            return CLAP_PORT_STEREO;
            break;
        default:
            return nullptr;
            break;
    }
}

AudioPortInfo::AudioPortInfo(const clap_audio_port_info_t& original)
    : id(original.id),
      name(original.name),
      flags(original.flags),
      channel_count(original.channel_count),
      port_type(parse_port_type(original.port_type)),
      in_place_pair(original.in_place_pair) {}

}  // namespace audio_ports
}  // namespace ext
}  // namespace clap
