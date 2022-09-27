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
// GNU General Public License for more destates.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <clap/ext/state.h>

#include "../../common.h"
#include "../stream.h"

// Serialization messages for `clap/ext/state.h`

namespace clap {
namespace ext {
namespace state {

namespace plugin {

/**
 * The response to the `clap::ext::state::Save` message defined below.
 */
struct SaveResponse {
    std::optional<clap::stream::Stream> result;

    template <typename S>
    void serialize(S& s) {
        s.ext(result, bitsery::ext::InPlaceOptional(),
              [](S& s, auto& v) { s.object(v); });
    }
};

/**
 * Message struct for `clap_plugin_state::save()`.
 */
struct Save {
    using Response = SaveResponse;

    native_size_t instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
    }
};

/**
 * Message struct for `clap_plugin_state::load()`.
 */
struct Load {
    using Response = PrimitiveResponse<bool>;

    native_size_t instance_id;
    clap::stream::Stream stream;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.object(stream);
    }
};

}  // namespace plugin

namespace host {

/**
 * Message struct for `clap_host_state::mark_dirty()`.
 */
struct MarkDirty {
    using Response = Ack;

    native_size_t owner_instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(owner_instance_id);
    }
};

}  // namespace host

}  // namespace state
}  // namespace ext
}  // namespace clap
