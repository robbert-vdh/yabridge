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

#include <optional>
#include <string>
#include <vector>

#include <clap/ext/note-ports.h>

#include "../../../bitsery/ext/in-place-optional.h"
#include "../../common.h"

// Serialization messages for `clap/ext/note-ports.h`

namespace clap {
namespace ext {
namespace note_ports {

/**
 * A serializable version of `clap_note_port_info` that owns all of the data it
 * references.
 */
struct NotePortInfo {
    /**
     * Parse a native `clap_note_port_info` struct so it can be serialized and
     * sent to the Wine plugin host.
     */
    NotePortInfo(const clap_note_port_info_t& original);

    /**
     * Default constructor for bitsery.
     */
    NotePortInfo() {}

    /**
     * Write the stored information to a host provided info struct.
     */
    void reconstruct(clap_note_port_info_t& port_info) const;

    clap_id id;
    uint32_t supported_dialects;
    uint32_t preferred_dialect;
    std::string name;

    template <typename S>
    void serialize(S& s) {
        s.value4b(id);
        s.value4b(supported_dialects);
        s.value4b(preferred_dialect);
        s.text1b(name, 4096);
    }
};

namespace plugin {

/**
 * Message struct for `clap_plugin_note_ports::count()`.
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
 * The response to the `clap::ext::note_ports::plugin::Get` message defined
 * below.
 */
struct GetResponse {
    std::optional<NotePortInfo> result;

    template <typename S>
    void serialize(S& s) {
        s.ext(result, bitsery::ext::InPlaceOptional());
    }
};

/**
 * Message struct for `clap_plugin_note_ports::get()`.
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
 * Message struct for `clap_host_note_ports::supported_dialects()`.
 */
struct SupportedDialects {
    using Response = PrimitiveResponse<uint32_t>;

    native_size_t owner_instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(owner_instance_id);
    }
};

/**
 * Message struct for `clap_host_note_ports::rescan()`.
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

}  // namespace note_ports
}  // namespace ext
}  // namespace clap
