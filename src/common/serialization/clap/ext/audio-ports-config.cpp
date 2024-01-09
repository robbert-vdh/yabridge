// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2024 Robbert van der Helm
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

#include "audio-ports-config.h"

#include "../../../utils.h"

namespace clap {
namespace ext {
namespace audio_ports_config {

AudioPortsConfig::AudioPortsConfig(const clap_audio_ports_config_t& original)
    : id(original.id),
      name(original.name),
      input_port_count(original.input_port_count),
      output_port_count(original.output_port_count),
      has_main_input(original.has_main_input),
      main_input_channel_count(original.main_input_channel_count),
      main_input_port_type(clap::ext::audio_ports::parse_audio_port_type(
          original.main_input_port_type)),
      has_main_output(original.has_main_output),
      main_output_channel_count(original.main_output_channel_count),
      main_output_port_type(clap::ext::audio_ports::parse_audio_port_type(
          original.main_output_port_type)) {}

void AudioPortsConfig::reconstruct(clap_audio_ports_config_t& config) const {
    config = clap_audio_ports_config_t{};
    config.id = id;
    strlcpy_buffer<sizeof(config.name)>(config.name, name);
    config.input_port_count = input_port_count;
    config.output_port_count = output_port_count;
    config.has_main_input = has_main_input;
    config.main_input_channel_count = main_input_channel_count;
    config.main_input_port_type =
        clap::ext::audio_ports::audio_port_type_to_string(main_input_port_type);
    config.has_main_output = has_main_output;
    config.main_output_channel_count = main_output_channel_count;
    config.main_output_port_type =
        clap::ext::audio_ports::audio_port_type_to_string(
            main_output_port_type);
}

}  // namespace audio_ports_config
}  // namespace ext
}  // namespace clap
