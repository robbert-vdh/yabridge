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

ClapHostExtensions::ClapHostExtensions(const clap_host& host) noexcept
    : audio_ports(static_cast<const clap_host_audio_ports_t*>(
          host.get_extension(&host, CLAP_EXT_AUDIO_PORTS))),
      note_ports(static_cast<const clap_host_note_ports_t*>(
          host.get_extension(&host, CLAP_EXT_NOTE_PORTS))) {}

ClapHostExtensions::ClapHostExtensions() noexcept {}

clap::host::SupportedHostExtensions ClapHostExtensions::supported()
    const noexcept {
    return clap::host::SupportedHostExtensions{
        .supports_audio_ports = audio_ports != nullptr,
        .supports_note_ports = note_ports != nullptr};
}

clap_plugin_proxy::clap_plugin_proxy(ClapPluginBridge& bridge,
                                     size_t instance_id,
                                     clap::plugin::Descriptor descriptor,
                                     const clap_host_t* host)
    : host_(host),
      bridge_(bridge),
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
      ext_audio_ports_vtable(clap_plugin_audio_ports_t{
          .count = ext_audio_ports_count,
          .get = ext_audio_ports_get,
      }),
      ext_note_ports_vtable(clap_plugin_note_ports_t{
          .count = ext_note_ports_count,
          .get = ext_note_ports_get,
      }),
      // These function objects are relatively large, and we probably won't be
      // getting that many of them
      pending_callbacks_(128) {}

bool CLAP_ABI clap_plugin_proxy::plugin_init(const struct clap_plugin* plugin) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<clap_plugin_proxy*>(plugin->plugin_data);

    // At this point we are allowed to query the host for extension structs.
    // We'll store pointers to the host's extensions vtables, and then send
    // whether or not those extensions were supported as booleans to the Wine
    // plugin host so it can expose the same interfaces there.
    self->extensions_ = ClapHostExtensions(*self->host_);

    const clap::plugin::InitResponse response =
        self->bridge_.send_main_thread_message(clap::plugin::Init{
            .instance_id = self->instance_id(),
            .supported_host_extensions = self->extensions_.supported()});

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
    auto self = static_cast<clap_plugin_proxy*>(plugin->plugin_data);

    const clap::plugin::ActivateResponse response =
        self->bridge_.send_main_thread_message(
            clap::plugin::Activate{.instance_id = self->instance_id(),
                                   .sample_rate = sample_rate,
                                   .min_frames_count = min_frames_count,
                                   .max_frames_count = max_frames_count});

    // The shared memory audio buffers are allocated here so we can use them
    // during audio processing
    if (response.updated_audio_buffers_config) {
        if (!self->process_buffers_) {
            self->process_buffers_.emplace(
                *response.updated_audio_buffers_config);
        } else {
            self->process_buffers_->resize(
                *response.updated_audio_buffers_config);
        }
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
    assert(plugin && plugin->plugin_data && id);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    // TODO: When implementing the GUI option, add a `clap_no_scaling` option to
    //       disable HiDPI scaling just like we have for VST3. Or rename the
    //       existing one.
    const void* extension_ptr = nullptr;
    if (self->supported_extensions_.supports_audio_ports &&
        strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
        extension_ptr = &self->ext_audio_ports_vtable;
    } else if (self->supported_extensions_.supports_note_ports &&
               strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) {
        extension_ptr = &self->ext_note_ports_vtable;
    }

    self->bridge_.logger_.log_extension_query("clap_plugin::get_extension",
                                              extension_ptr, id);

    return extension_ptr;
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

uint32_t CLAP_ABI
clap_plugin_proxy::ext_audio_ports_count(const clap_plugin_t* plugin,
                                         bool is_input) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    return self->bridge_.send_main_thread_message(
        clap::ext::audio_ports::plugin::Count{
            .instance_id = self->instance_id(), .is_input = is_input});
}

bool CLAP_ABI
clap_plugin_proxy::ext_audio_ports_get(const clap_plugin_t* plugin,
                                       uint32_t index,
                                       bool is_input,
                                       clap_audio_port_info_t* info) {
    assert(plugin && plugin->plugin_data && info);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    const clap::ext::audio_ports::plugin::GetResponse response =
        self->bridge_.send_main_thread_message(
            clap::ext::audio_ports::plugin::Get{
                .instance_id = self->instance_id(),
                .index = index,
                .is_input = is_input});
    if (response.result) {
        response.result->reconstruct(*info);

        return true;
    } else {
        return false;
    }
}

uint32_t CLAP_ABI
clap_plugin_proxy::ext_note_ports_count(const clap_plugin_t* plugin,
                                        bool is_input) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    return self->bridge_.send_main_thread_message(
        clap::ext::note_ports::plugin::Count{.instance_id = self->instance_id(),
                                             .is_input = is_input});
}

bool CLAP_ABI
clap_plugin_proxy::ext_note_ports_get(const clap_plugin_t* plugin,
                                      uint32_t index,
                                      bool is_input,
                                      clap_note_port_info_t* info) {
    assert(plugin && plugin->plugin_data && info);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    const clap::ext::note_ports::plugin::GetResponse response =
        self->bridge_.send_main_thread_message(
            clap::ext::note_ports::plugin::Get{
                .instance_id = self->instance_id(),
                .index = index,
                .is_input = is_input});
    if (response.result) {
        response.result->reconstruct(*info);

        return true;
    } else {
        return false;
    }
}
