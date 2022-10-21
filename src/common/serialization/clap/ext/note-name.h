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

#include <clap/ext/note-name.h>

#include "../../../bitsery/ext/in-place-optional.h"
#include "../../common.h"

// Serialization messages for `clap/ext/note-name.h`

namespace clap {
namespace ext {
namespace note_name {

/**
 * A serializable version of `clap_note_name_t` that owns all of the data it
 * references.
 */
struct NoteName {
    /**
     * Parse a native `clap_note_name_t` struct so it can be serialized and sent
     * to the Wine plugin host.
     */
    NoteName(const clap_note_name_t& original);

    /**
     * Default constructor for bitsery.
     */
    NoteName() {}

    /**
     * Write the stored information to a host provided struct.
     */
    void reconstruct(clap_note_name_t& note_name) const;

    std::string name;
    int16_t port;
    int16_t key;
    int16_t channel;

    template <typename S>
    void serialize(S& s) {
        s.text1b(name, 4096);
        s.value2b(port);
        s.value2b(key);
        s.value2b(channel);
    }
};

namespace plugin {

/**
 * Message struct for `clap_plugin_note_name::count()`.
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
 * The response to the `clap::ext::note_name::plugin::Get` message defined
 * below.
 */
struct GetResponse {
    std::optional<NoteName> result;

    template <typename S>
    void serialize(S& s) {
        s.ext(result, bitsery::ext::InPlaceOptional());
    }
};

/**
 * Message struct for `clap_plugin_note_name::get()`.
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

}  // namespace plugin

namespace host {

/**
 * Message struct for `clap_host_note_name::changed()`.
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

}  // namespace note_name
}  // namespace ext
}  // namespace clap
