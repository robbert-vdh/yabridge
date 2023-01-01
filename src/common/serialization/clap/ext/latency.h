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
// GNU General Public License for more delatencys.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <clap/ext/latency.h>

#include "../../common.h"

// Serialization messages for `clap/ext/latency.h`

namespace clap {
namespace ext {
namespace latency {

namespace plugin {

/**
 * Message struct for `clap_plugin_latency::get()`.
 */
struct Get {
    using Response = PrimitiveResponse<uint32_t>;

    native_size_t instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
    }
};

}  // namespace plugin

namespace host {

/**
 * Message struct for `clap_host_latency::changed()`.
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

}  // namespace latency
}  // namespace ext
}  // namespace clap
