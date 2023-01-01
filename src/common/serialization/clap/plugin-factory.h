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

#include "../common.h"
#include "host.h"
#include "plugin.h"

// Serialization messages for `clap/plugin-factory.h`

namespace clap {
namespace plugin_factory {

/**
 * The response to the `clap::plugin_factory::List` message defined below.
 */
struct ListResponse {
    /**
     * The descriptors for the plugins in the factory. This will be a nullopt if
     * the plugin does not support the plugin factory.
     */
    std::optional<std::vector<clap::plugin::Descriptor>> descriptors;

    template <typename S>
    void serialize(S& s) {
        s.ext(descriptors, bitsery::ext::InPlaceOptional{},
              [](S& s, auto& v) { s.container(v, 8192); });
    }
};

/**
 * Message combining `clap_plugin_factory::count()` with
 * `clap_plugin_factory::get()` to get all plugin descriptors in one go. Will
 * return a nullopt if the plugin does not support the plugin factory.
 */
struct List {
    using Response = ListResponse;

    // Since we send this to a specific CLAP plugin library, there are no
    // parameters here
    template <typename S>
    void serialize(S&) {}
};

/**
 * The response to the `clap::plugin_factory::Create` message defined below.
 */
struct CreateResponse {
    /**
     * The new plugin instance's ID, if it was initialized correctly. We'll
     * assume the instance's plugin descriptor is the same as the one from the
     * factory.
     */
    std::optional<native_size_t> instance_id;

    template <typename S>
    void serialize(S& s) {
        s.ext(instance_id, bitsery::ext::InPlaceOptional{},
              [](S& s, auto& v) { s.value8b(v); });
    }
};

/**
 * Message struct for `clap_plugin_factory::create()`. Contains information
 * about the host for the `clap_host_t*`. If the plugin instance was created
 * successfully, then the Wine host side will generate a unique identifier for
 * the instance that is used to refer to it in further messages.
 */
struct Create {
    using Response = CreateResponse;

    /**
     * Information about the native host that can be used to construct a
     * `clap_host_t*` proxy.
     */
    clap::host::Host host;

    std::string plugin_id;

    template <typename S>
    void serialize(S& s) {
        s.object(host);
        s.text1b(plugin_id, 4096);
    }
};

}  // namespace plugin_factory
}  // namespace clap
