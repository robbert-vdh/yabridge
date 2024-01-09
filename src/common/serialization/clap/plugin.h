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

#include <bitsery/traits/vector.h>
#include <clap/plugin.h>

#include "../../bitsery/ext/in-place-optional.h"
#include "../audio-shm.h"
#include "../common.h"
#include "host.h"
#include "process.h"
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
    // Don't forget to add new extensions to below method
    bool supports_audio_ports = false;
    bool supports_audio_ports_config = false;
    bool supports_gui = false;
    bool supports_latency = false;
    bool supports_note_name = false;
    bool supports_note_ports = false;
    bool supports_params = false;
    bool supports_render = false;
    bool supports_state = false;
    bool supports_tail = false;
    bool supports_voice_info = false;

    /**
     * Get a list of `<bool, extension_name>` tuples for the supported
     * extensions. Used during logging.
     */
    std::array<std::pair<bool, const char*>, 11> list() const noexcept;

    template <typename S>
    void serialize(S& s) {
        s.value1b(supports_audio_ports);
        s.value1b(supports_audio_ports_config);
        s.value1b(supports_gui);
        s.value1b(supports_latency);
        s.value1b(supports_note_name);
        s.value1b(supports_note_ports);
        s.value1b(supports_params);
        s.value1b(supports_render);
        s.value1b(supports_state);
        s.value1b(supports_tail);
        s.value1b(supports_voice_info);
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

/**
 * The response to the `clap::plugin::Plugin` message defined below. This
 * `Response` object contains pointers into an already allocated
 * `clap::process::Process` object so the data can be serialized in place as an
 * optimization.
 */
struct ProcessResponse {
    clap_process_status result;
    clap::process::Process::Response output_data;

    template <typename S>
    void serialize(S& s) {
        s.value4b(result);
        s.object(output_data);
    }
};

/**
 * Message struct for `clap_plugin::stop_processing()`. This
 * `clap::process::Process` object wraps around all input audio buffersand
 * events along with the other process data provided by the host so we can send
 * it to the Wine plugin host. We can then use
 * `clap::process::Process::reconstruct()` on the Wine plugin host side to
 * reconstruct the original `clap_process_t` object, and we then finally use
 * `clap::process::Process::create_response()` to create a response object that
 * we can write the plugin's changes back to the `clap_process_t` object
 * provided by the host.
 */
struct Process {
    using Response = ProcessResponse;

    native_size_t instance_id;

    clap::process::Process process;

    /**
     * We'll periodically synchronize the realtime priority setting of the
     * host's audio thread with the Wine plugin host. We'll do this
     * approximately every ten seconds, as doing this getting and setting
     * scheduler information has a non trivial amount of overhead (even if it's
     * only a single microsoecond).
     */
    std::optional<int> new_realtime_priority;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.object(process);
        s.ext(new_realtime_priority, bitsery::ext::InPlaceOptional{},
              [](S& s, int& priority) { s.value4b(priority); });
    }
};

}  // namespace plugin
}  // namespace clap
