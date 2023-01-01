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

#include <clap/ext/params.h>

#include "../../../bitsery/ext/in-place-optional.h"
#include "../../common.h"
#include "../events.h"

// Serialization messages for `clap/ext/params.h`

namespace clap {
namespace ext {
namespace params {

/**
 * A serializable version of `clap_param_info` that owns all of the data it
 * references.
 */
struct ParamInfo {
    /**
     * Parse a native `clap_param_info` struct so it can be serialized and sent
     * to the Wine plugin host.
     */
    ParamInfo(const clap_param_info_t& original);

    /**
     * Default constructor for bitsery.
     */
    ParamInfo() {}

    /**
     * Write the stored information to a host provided info struct.
     */
    void reconstruct(clap_param_info_t& port_info) const;

    clap_id id;
    clap_param_info_flags flags;
    // This is the `void*` provided by the plugin, as an integer
    native_size_t cookie;
    std::string name;
    std::string module;
    double min_value;
    double max_value;
    double default_value;

    template <typename S>
    void serialize(S& s) {
        s.value4b(id);
        s.value4b(flags);
        s.value8b(cookie);
        s.text1b(name, 4096);
        s.text1b(module, 4096);
        s.value8b(min_value);
        s.value8b(max_value);
        s.value8b(default_value);
    }
};

namespace plugin {

/**
 * Message struct for `clap_plugin_params::count()`.
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
 * The response to the `clap::ext::params::plugin::GetInfo` message defined
 * below.
 */
struct GetInfoResponse {
    std::optional<ParamInfo> result;

    template <typename S>
    void serialize(S& s) {
        s.ext(result, bitsery::ext::InPlaceOptional());
    }
};

/**
 * Message struct for `clap_plugin_params::get_info()`.
 */
struct GetInfo {
    using Response = GetInfoResponse;

    native_size_t instance_id;
    uint32_t param_index;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value4b(param_index);
    }
};

/**
 * The response to the `clap::ext::params::plugin::GetValue` message defined
 * below.
 */
struct GetValueResponse {
    std::optional<double> result;

    template <typename S>
    void serialize(S& s) {
        s.ext(result, bitsery::ext::InPlaceOptional(),
              [](S& s, auto& v) { s.value8b(v); });
    }
};

/**
 * Message struct for `clap_plugin_params::get_value()`.
 */
struct GetValue {
    using Response = GetValueResponse;

    native_size_t instance_id;
    clap_id param_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value4b(param_id);
    }
};

/**
 * The response to the `clap::ext::params::plugin::ValueToText` message defined
 * below.
 */
struct ValueToTextResponse {
    std::optional<std::string> result;

    template <typename S>
    void serialize(S& s) {
        s.ext(result, bitsery::ext::InPlaceOptional(),
              [](S& s, auto& v) { s.text1b(v, 4096); });
    }
};

/**
 * Message struct for `clap_plugin_params::value_to_text()`.
 */
struct ValueToText {
    using Response = ValueToTextResponse;

    native_size_t instance_id;
    clap_id param_id;
    double value;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value4b(param_id);
        s.value8b(value);
    }
};

/**
 * The response to the `clap::ext::params::plugin::TextToValue` message defined
 * below.
 */
struct TextToValueResponse {
    std::optional<double> result;

    template <typename S>
    void serialize(S& s) {
        s.ext(result, bitsery::ext::InPlaceOptional(),
              [](S& s, auto& v) { s.value8b(v); });
    }
};

/**
 * Message struct for `clap_plugin_params::text_to_value()`.
 */
struct TextToValue {
    using Response = TextToValueResponse;

    native_size_t instance_id;
    clap_id param_id;
    std::string display;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value4b(param_id);
        s.text1b(display, 4096);
    }
};

/**
 * The response to the `clap::ext::params::plugin::Flush` message defined below.
 */
struct FlushResponse {
    clap::events::EventList out;

    template <typename S>
    void serialize(S& s) {
        s.object(out);
    }
};

/**
 * Message struct for `clap_plugin_params::flush()`.
 */
struct Flush {
    using Response = FlushResponse;

    native_size_t instance_id;
    clap::events::EventList in;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.object(in);
    }
};

}  // namespace plugin

namespace host {

/**
 * Message struct for `clap_host_params::rescan()`.
 */
struct Rescan {
    using Response = Ack;

    native_size_t owner_instance_id;
    clap_param_rescan_flags flags;

    template <typename S>
    void serialize(S& s) {
        s.value8b(owner_instance_id);
        s.value4b(flags);
    }
};

/**
 * Message struct for `clap_host_params::clear()`.
 */
struct Clear {
    using Response = Ack;

    native_size_t owner_instance_id;
    clap_id param_id;
    clap_param_clear_flags flags;

    template <typename S>
    void serialize(S& s) {
        s.value8b(owner_instance_id);
        s.value4b(param_id);
        s.value4b(flags);
    }
};

/**
 * Message struct for `clap_host_params::request_flush()`.
 */
struct RequestFlush {
    using Response = Ack;

    native_size_t owner_instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(owner_instance_id);
    }
};

}  // namespace host

}  // namespace params
}  // namespace ext
}  // namespace clap
