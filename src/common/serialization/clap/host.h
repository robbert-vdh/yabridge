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

#include <bitsery/traits/vector.h>
#include <clap/plugin.h>

#include "../../bitsery/ext/in-place-optional.h"
#include "../common.h"
#include "version.h"

// Serialization messages for `clap/host.h`

namespace clap {
namespace host {

/**
 * A serializable version of `clap_host_t`'s data fields so we can proxy the
 * host on the Wine side.
 */
struct Host {
    /**
     * Parse a host descriptor so it can be serialized and sent to the Wine
     * plugin host.
     */
    Host(const clap_host_t& original);

    /**
     * Default constructor for bitsery.
     */
    Host() {}

    /**
     * We'll report the maximum of the plugin's supported CLAP version and
     * yabridge's supported CLAP version. I don't know why there's a version
     * field here when the entry point also has a version field.
     */
    clap_version_t clap_version;

    std::string name;
    std::optional<std::string> vendor;
    std::optional<std::string> url;
    std::string version;

    template <typename S>
    void serialize(S& s) {
        s.object(clap_version);

        s.text1b(name, 4096);
        s.ext(vendor, bitsery::ext::InPlaceOptional(),
              [](S& s, auto& v) { s.text1b(v, 4096); });
        s.ext(url, bitsery::ext::InPlaceOptional(),
              [](S& s, auto& v) { s.text1b(v, 4096); });
        s.text1b(version, 4096);
    }
};

/**
 * Extensions supported by the host. This can only be queried in
 * `clap_plugin::init()` so it cannot be part of `Host`. We'll create make these
 * same extensions available to the bridged CLAP plugins using proxies.
 */
struct SupportedHostExtensions {
    /**
     * Read the supported extensions from a native CLAP host. This is only valid
     * once the native host has called `clap_host::init()`.
     */
    SupportedHostExtensions(const clap_host& host);

    SupportedHostExtensions() noexcept {}

    // Don't forget to add new extensions to the log output
    bool supports_audio_ports = false;

    template <typename S>
    void serialize(S& s) {
        s.value1b(supports_audio_ports);
    }
};

/**
 * Message struct for `clap_host::request_restart()`.
 */
struct RequestRestart {
    using Response = Ack;

    native_size_t owner_instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(owner_instance_id);
    }
};

/**
 * Message struct for `clap_host::request_process()`.
 */
struct RequestProcess {
    using Response = Ack;

    native_size_t owner_instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(owner_instance_id);
    }
};

// `clap_host::request_callback()` is of course handled entirely on the Wine
// plugin host side

}  // namespace host
}  // namespace clap
