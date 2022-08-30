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

// Serialization messages for `clap/plugin.h`

namespace clap {
namespace plugin {

/**
 * Owned wrapper around `clap_plugin_descriptor` for serialization purposes.
 */
struct descriptor {
    /**
     * Parse a plugin-provided descriptor so it can be serialized and sent to
     * the native CLAP plugin.
     */
    descriptor(const clap_plugin_descriptor_t& original);

    /**
     * Default constructor for bitsery.
     */
    descriptor() {}

    /**
     * We'll report the maximum of the plugin's supported CLAP version and
     * yabridge's supported CLAP version. I don't know why there's a version
     * field here when the entry point also has a version field.
     */
    clap_version_t clap_version;

    std::string id;
    std::string name;
    std::optional<std::string> vendor;
    std::optional<std::string> url;
    std::optional<std::string> manual_url;
    std::optional<std::string> support_url;
    std::optional<std::string> version;
    std::optional<std::string> description;

    std::vector<std::string> features;

    /**
     * Create a CLAP plugin descriptor from this wrapper. This contains pointers
     * to this object's fields, so this descriptor is only valid as long as this
     * object is alive and doesn't get moved.
     */
    clap_plugin_descriptor_t get() const;

    template <typename S>
    void serialize(S& s) {
        s.object(clap_version);

        s.text1b(id, 4096);
        s.text1b(name, 4096);
        s.ext(vendor, bitsery::ext::InPlaceOptional(),
              [](S& s, auto& v) { s.text1b(v, 4096); });
        s.ext(url, bitsery::ext::InPlaceOptional(),
              [](S& s, auto& v) { s.text1b(v, 4096); });
        s.ext(manual_url, bitsery::ext::InPlaceOptional(),
              [](S& s, auto& v) { s.text1b(v, 4096); });
        s.ext(support_url, bitsery::ext::InPlaceOptional(),
              [](S& s, auto& v) { s.text1b(v, 4096); });
        s.ext(version, bitsery::ext::InPlaceOptional(),
              [](S& s, auto& v) { s.text1b(v, 4096); });
        s.ext(description, bitsery::ext::InPlaceOptional(),
              [](S& s, auto& v) { s.text1b(v, 4096); });

        s.container(features, 4096, [](S& s, auto& v) { s.text1b(v, 4096); });
    }

   private:
    /**
     * A null terminated array of pointers to the features in `features`.
     * Populated as part of `get()`.
     */
    mutable std::vector<const char*> features_ptrs;
};

}  // namespace plugin
}  // namespace clap

template <typename S>
void serialize(S& s, clap_version_t& version) {
    s.value4b(version.major);
    s.value4b(version.minor);
    s.value4b(version.revision);
}
