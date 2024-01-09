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

#include "host.h"

#include <clap/ext/audio-ports-config.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/latency.h>
#include <clap/ext/log.h>
#include <clap/ext/note-name.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>
#include <clap/ext/voice-info.h>

namespace clap {
namespace host {

Host::Host(const clap_host_t& original)
    : clap_version(original.clap_version),
      name((assert(original.name), original.name)),
      vendor(original.vendor ? std::optional(original.vendor) : std::nullopt),
      url(original.url ? std::optional(original.url) : std::nullopt),
      version((assert(original.version), original.version)) {}

std::array<std::pair<bool, const char*>, 11> SupportedHostExtensions::list()
    const noexcept {
    return {std::pair(supports_audio_ports, CLAP_EXT_AUDIO_PORTS),
            std::pair(supports_audio_ports_config, CLAP_EXT_AUDIO_PORTS_CONFIG),
            std::pair(supports_gui, CLAP_EXT_GUI),
            std::pair(supports_latency, CLAP_EXT_LATENCY),
            std::pair(supports_log, CLAP_EXT_LOG),
            std::pair(supports_note_name, CLAP_EXT_NOTE_NAME),
            std::pair(supports_note_ports, CLAP_EXT_NOTE_PORTS),
            std::pair(supports_params, CLAP_EXT_PARAMS),
            std::pair(supports_state, CLAP_EXT_STATE),
            std::pair(supports_tail, CLAP_EXT_TAIL),
            std::pair(supports_voice_info, CLAP_EXT_VOICE_INFO)};
}

}  // namespace host
}  // namespace clap
