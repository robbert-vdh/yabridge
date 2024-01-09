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

#pragma once

#include <clap/ext/voice-info.h>

#include "../../common.h"

// Serialization messages for `clap/ext/voice-info.h`

namespace clap {
namespace ext {
namespace voice_info {

namespace plugin {

/**
 * The response to the `clap::ext::voice_info::plugin::Get` message defined
 * below.
 */
struct GetResponse {
    std::optional<clap_voice_info_t> result;

    template <typename S>
    void serialize(S& s) {
        s.ext(result, bitsery::ext::InPlaceOptional());
    }
};

/**
 * Message struct for `clap_plugin_voice_info::get()`.
 */
struct Get {
    using Response = GetResponse;

    native_size_t instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
    }
};

}  // namespace plugin

namespace host {

/**
 * Message struct for `clap_host_voice_info::changed()`.
 */
struct Changed {
    using Response = Ack;

    native_size_t owner_instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(owner_instance_id);
    }
};

}  // namespace host

}  // namespace voice_info
}  // namespace ext
}  // namespace clap

template <typename S>
void serialize(S& s, clap_voice_info_t& info) {
    s.value4b(info.voice_count);
    s.value4b(info.voice_capacity);
    s.value8b(info.flags);
}
