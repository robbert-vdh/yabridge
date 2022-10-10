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

#include <clap/ext/render.h>

#include "../../common.h"

// Serialization messages for `clap/ext/render.h`

namespace clap {
namespace ext {
namespace render {

namespace plugin {

/**
 * Message struct for `clap_plugin_render::has_hard_realtime_requirement()`.
 */
struct HasHardRealtimeRequirement {
    using Response = PrimitiveResponse<bool>;

    native_size_t instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
    }
};

/**
 * Message struct for `clap_plugin_render::set()`.
 */
struct Set {
    using Response = PrimitiveResponse<bool>;

    native_size_t instance_id;

    clap_plugin_render_mode mode;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value4b(mode);
    }
};

}  // namespace plugin

}  // namespace render
}  // namespace ext
}  // namespace clap
