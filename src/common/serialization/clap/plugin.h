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
#include "../audio-shm.h"
#include "../common.h"
#include "host.h"
#include "version.h"

// Serialization messages for `clap/plugin.h`

namespace clap {
namespace plugin {

/**
 * Owned wrapper around `clap_plugin_descriptor` for serialization purposes.
 */
struct Descriptor {
    /**
     * Parse a plugin-provided descriptor so it can be serialized and sent to
     * the native CLAP plugin.
     */
    Descriptor(const clap_plugin_descriptor_t& original);

    /**
     * Default constructor for bitsery.
     */
    Descriptor() {}

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
    const clap_plugin_descriptor_t* get() const;

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
    /**
     * The CLAP descriptor populated and returned from `get()`.
     */
    mutable clap_plugin_descriptor_t clap_descriptor;
};

/**
 * Extensions supported by the plugin. Queried after `clap_plugin::init()` and
 * created by `ClapPluginExtensions::supported()`.
 */
struct SupportedPluginExtensions {
    // Don't forget to add new extensions to the log output
    bool supports_audio_ports = false;

    template <typename S>
    void serialize(S& s) {
        s.value1b(supports_audio_ports);
    }
};

/**
 * The response to the `clap::plugin::Init` message defined below.
 */
struct InitResponse {
    bool result;
    SupportedPluginExtensions supported_plugin_extensions;

    template <typename S>
    void serialize(S& s) {
        s.value1b(result);
        s.object(supported_plugin_extensions);
    }
};

/**
 * Message struct for `clap_plugin::init()`. This is where we set the supported
 * host extensions on the Wine side, and query the plugin's supported extensions
 * so we can proxy them.
 */
struct Init {
    using Response = InitResponse;

    native_size_t instance_id;
    clap::host::SupportedHostExtensions supported_host_extensions;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.object(supported_host_extensions);
    }
};

/**
 * Message struct for `clap_plugin::destroy()`. The Wine plugin host should
 * clean up the plugin, and everything is also cleaned up on the plugin side
 * after receiving acknowledgement.
 */
struct Destroy {
    using Response = Ack;

    native_size_t instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
    }
};

/**
 * The response to the `clap::plugin::Activate` message defined below.
 */
struct ActivateResponse {
    bool result;
    /**
     * Only set if activating was successful and the config is different from a
     * previously returned config.
     */
    std::optional<AudioShmBuffer::Config> updated_audio_buffers_config;

    template <typename S>
    void serialize(S& s) {
        s.value1b(result);
        s.ext(updated_audio_buffers_config, bitsery::ext::InPlaceOptional{});
    }
};

/**
 * Message struct for `clap_plugin::activate()`. This is where shared memory
 * audio buffers are set up.
 */
struct Activate {
    using Response = ActivateResponse;

    native_size_t instance_id;

    double sample_rate;
    uint32_t min_frames_count;
    uint32_t max_frames_count;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value8b(sample_rate);
        s.value4b(min_frames_count);
        s.value4b(max_frames_count);
    }
};

/**
 * Message struct for `clap_plugin::deactivate()`.
 */
struct Deactivate {
    using Response = Ack;

    native_size_t instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
    }
};

/**
 * Message struct for `clap_plugin::start_processing()`.
 */
struct StartProcessing {
    using Response = PrimitiveResponse<bool>;

    native_size_t instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
    }
};

/**
 * Message struct for `clap_plugin::stop_processing()`.
 */
struct StopProcessing {
    using Response = Ack;

    native_size_t instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
    }
};

/**
 * Message struct for `clap_plugin::reset()`.
 */
struct Reset {
    using Response = Ack;

    native_size_t instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
    }
};

// TODO: Process

}  // namespace plugin
}  // namespace clap
