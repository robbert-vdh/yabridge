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

#include "plugin-proxy.h"

#include "../clap.h"

clap_plugin_proxy::clap_plugin_proxy(ClapPluginBridge& bridge,
                                     size_t instance_id,
                                     clap::plugin::Descriptor descriptor,
                                     const clap_host_t* host)
    : bridge_(bridge),
      instance_id_(instance_id),
      descriptor_(std::move(descriptor)),
      plugin_vtable_(clap_plugin_t{
          .desc = descriptor_.get(),
          .plugin_data = this,
          .init = plugin_init,
          .destroy = plugin_destroy,
          .activate = plugin_activate,
          .deactivate = plugin_deactivate,
          .start_processing = plugin_start_processing,
          .stop_processing = plugin_stop_processing,
          .reset = plugin_reset,
          .process = plugin_process,
          .get_extension = plugin_get_extension,
          .on_main_thread = plugin_on_main_thread,
      }),
      host_(host),
      // These function objects are relatively large, and we probably won't be
      // getting that many of them
      pending_callbacks_(128) {}

bool CLAP_ABI clap_plugin_proxy::plugin_init(const struct clap_plugin* plugin) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<clap_plugin_proxy*>(plugin->plugin_data);

    const clap::plugin::InitResponse response =
        self->bridge_.send_main_thread_message(
            clap::plugin::Init{.instance_id = self->instance_id(),
                               .supported_host_extensions = *self->host_});

    // This determines which extensions the host is allowed to query in
    // `clap_plugin::get_extension()`
    self->supported_extensions_ = response.supported_plugin_extensions;

    return response.result;
}

void CLAP_ABI
clap_plugin_proxy::plugin_destroy(const struct clap_plugin* plugin) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    // This will clean everything related to this instance up on the Wine plugin
    // host side
    self->bridge_.send_main_thread_message(
        clap::plugin::Destroy{.instance_id = self->instance_id()});

    // And this deallocates and destroys `self`
    self->bridge_.unregister_plugin_proxy(self->instance_id());
}

bool CLAP_ABI clap_plugin_proxy::plugin_activate(
    const struct clap_plugin* plugin,
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    double sample_rate,
    uint32_t min_frames_count,
    uint32_t max_frames_count) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    const clap::plugin::ActivateResponse response =
        self->bridge_.send_main_thread_message(
            clap::plugin::Activate{.instance_id = self->instance_id(),
                                   .sample_rate = sample_rate,
                                   .min_frames_count = min_frames_count,
                                   .max_frames_count = max_frames_count});

    if (response.updated_audio_buffers_config) {
        // TODO: Set up the shared memory audio buffers
    }

    return response.result;
}

void CLAP_ABI
clap_plugin_proxy::plugin_deactivate(const struct clap_plugin* plugin) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    self->bridge_.send_main_thread_message(
        clap::plugin::Deactivate{.instance_id = self->instance_id()});
}

bool CLAP_ABI
clap_plugin_proxy::plugin_start_processing(const struct clap_plugin* plugin) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    return self->bridge_.send_audio_thread_message(
        clap::plugin::StartProcessing{.instance_id = self->instance_id()});
}

void CLAP_ABI
clap_plugin_proxy::plugin_stop_processing(const struct clap_plugin* plugin) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    self->bridge_.send_audio_thread_message(
        clap::plugin::StopProcessing{.instance_id = self->instance_id()});
}

void CLAP_ABI
clap_plugin_proxy::plugin_reset(const struct clap_plugin* plugin) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    self->bridge_.send_audio_thread_message(
        clap::plugin::Reset{.instance_id = self->instance_id()});
}

clap_process_status CLAP_ABI
clap_plugin_proxy::plugin_process(const struct clap_plugin* plugin,
                                  const clap_process_t* process) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    // TODO: Implement
    return CLAP_PROCESS_ERROR;
}

const void* CLAP_ABI
clap_plugin_proxy::plugin_get_extension(const struct clap_plugin* plugin,
                                        const char* id) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    // TODO: Implement
    return nullptr;
}

void CLAP_ABI
clap_plugin_proxy::plugin_on_main_thread(const struct clap_plugin* plugin) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<clap_plugin_proxy*>(plugin->plugin_data);

    // Functions are pushed to this queue so they can be run on the host's main
    // thread
    HostCallback callback;
    while (self->pending_callbacks_.try_pop(callback)) {
        callback();
    }
}
