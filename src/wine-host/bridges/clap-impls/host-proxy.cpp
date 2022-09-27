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

#include "host-proxy.h"

#include "../../../common/serialization/clap/version.h"
#include "../clap.h"

clap_host_proxy::clap_host_proxy(ClapBridge& bridge,
                                 size_t owner_instance_id,
                                 clap::host::Host host_args)
    : bridge_(bridge),
      owner_instance_id_(owner_instance_id),
      host_args_(std::move(host_args)),
      host_vtable_(clap_host_t{
          .clap_version = clamp_clap_version(host_args_.clap_version),
          .host_data = this,
          // HACK: Certain plugins may have undesirable DAW-specific behaviour.
          //       Chromaphone 3 for instance has broken text input dialogs when
          //       using Bitwig.
          .name = bridge_.config_.hide_daw ? product_name_override
                                           : host_args_.name.c_str(),
          .vendor =
              bridge_.config_.hide_daw
                  ? vendor_name_override
                  : (host_args_.vendor ? host_args_.vendor->c_str() : nullptr),
          .url = host_args_.url ? host_args_.url->c_str() : nullptr,
          .version = host_args_.version.c_str(),
          .get_extension = host_get_extension,
          .request_restart = host_request_restart,
          .request_process = host_request_process,
          .request_callback = host_request_callback,
      }),
      ext_audio_ports_vtable(clap_host_audio_ports_t{
          .is_rescan_flag_supported = ext_audio_ports_is_rescan_flag_supported,
          .rescan = ext_audio_ports_rescan,
      }),
      ext_latency_vtable(clap_host_latency_t{
          .changed = ext_latency_changed,
      }),
      ext_note_ports_vtable(clap_host_note_ports_t{
          .supported_dialects = ext_note_ports_supported_dialects,
          .rescan = ext_note_ports_rescan,
      }),
      ext_params_vtable(clap_host_params_t{
          .rescan = ext_params_rescan,
          .clear = ext_params_clear,
          .request_flush = ext_params_request_flush,
      }),
      ext_state_vtable(clap_host_state_t{
          .mark_dirty = ext_state_mark_dirty,
      }),
      ext_tail_vtable(clap_host_tail_t{
          .changed = ext_tail_changed,
      }) {}

const void* CLAP_ABI
clap_host_proxy::host_get_extension(const struct clap_host* host,
                                    const char* extension_id) {
    assert(host && host->host_data && extension_id);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    const void* extension_ptr = nullptr;
    if (self->supported_extensions_.supports_audio_ports &&
        strcmp(extension_id, CLAP_EXT_AUDIO_PORTS) == 0) {
        extension_ptr = &self->ext_audio_ports_vtable;
    } else if (self->supported_extensions_.supports_latency &&
               strcmp(extension_id, CLAP_EXT_LATENCY) == 0) {
        extension_ptr = &self->ext_latency_vtable;
    } else if (self->supported_extensions_.supports_note_ports &&
               strcmp(extension_id, CLAP_EXT_NOTE_PORTS) == 0) {
        extension_ptr = &self->ext_note_ports_vtable;
    } else if (self->supported_extensions_.supports_params &&
               strcmp(extension_id, CLAP_EXT_PARAMS) == 0) {
        extension_ptr = &self->ext_params_vtable;
    } else if (self->supported_extensions_.supports_state &&
               strcmp(extension_id, CLAP_EXT_STATE) == 0) {
        extension_ptr = &self->ext_state_vtable;
    } else if (self->supported_extensions_.supports_tail &&
               strcmp(extension_id, CLAP_EXT_TAIL) == 0) {
        extension_ptr = &self->ext_tail_vtable;
    }

    self->bridge_.logger_.log_extension_query("clap_host::get_extension",
                                              extension_ptr, extension_id);

    return extension_ptr;
}

void CLAP_ABI
clap_host_proxy::host_request_restart(const struct clap_host* host) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    self->bridge_.send_main_thread_message(clap::host::RequestRestart{
        .owner_instance_id = self->owner_instance_id()});
}

void CLAP_ABI
clap_host_proxy::host_request_process(const struct clap_host* host) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    self->bridge_.send_main_thread_message(clap::host::RequestProcess{
        .owner_instance_id = self->owner_instance_id()});
}

void CLAP_ABI
clap_host_proxy::host_request_callback(const struct clap_host* host) {
    assert(host && host->host_data);
    auto self = static_cast<clap_host_proxy*>(host->host_data);

    self->bridge_.logger_.log_callback_request(self->owner_instance_id());

    // Only schedule a `clap_plugin::on_main_thread()` call if we don't already
    // have a pending one. This limits the number of unnecessarily stacked
    // calls.
    bool expected = false;
    if (self->has_pending_host_callbacks_.compare_exchange_strong(expected,
                                                                  true)) {
        // We're acquiring a lock on the instance and then move it into the task
        // to prevent this instance from being removed before this callback has
        // been run
        auto instance_lock =
            self->bridge_.get_instance(self->owner_instance_id());
        self->bridge_.main_context_.schedule_task(
            [self, instance_lock = std::move(instance_lock)]() {
                const auto& [instance, _] = instance_lock;
                self->has_pending_host_callbacks_.store(false);

                self->bridge_.logger_.log_on_main_thread(
                    self->owner_instance_id());

                instance.plugin->on_main_thread(instance.plugin.get());
            });
    }
}

bool CLAP_ABI clap_host_proxy::ext_audio_ports_is_rescan_flag_supported(
    const clap_host_t* host,
    uint32_t flag) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    return self->bridge_.send_main_thread_message(
        clap::ext::audio_ports::host::IsRescanFlagSupported{
            .owner_instance_id = self->owner_instance_id(), .flag = flag});
}

void CLAP_ABI clap_host_proxy::ext_audio_ports_rescan(const clap_host_t* host,
                                                      uint32_t flags) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    self->bridge_.send_main_thread_message(clap::ext::audio_ports::host::Rescan{
        .owner_instance_id = self->owner_instance_id(), .flags = flags});
}

void CLAP_ABI clap_host_proxy::ext_latency_changed(const clap_host_t* host) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    self->bridge_.send_main_thread_message(clap::ext::latency::host::Changed{
        .owner_instance_id = self->owner_instance_id()});
}

uint32_t CLAP_ABI
clap_host_proxy::ext_note_ports_supported_dialects(const clap_host_t* host) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    return self->bridge_.send_main_thread_message(
        clap::ext::note_ports::host::SupportedDialects{
            .owner_instance_id = self->owner_instance_id()});
}

void CLAP_ABI clap_host_proxy::ext_note_ports_rescan(const clap_host_t* host,
                                                     uint32_t flags) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    self->bridge_.send_main_thread_message(clap::ext::note_ports::host::Rescan{
        .owner_instance_id = self->owner_instance_id(), .flags = flags});
}

void CLAP_ABI
clap_host_proxy::ext_params_rescan(const clap_host_t* host,
                                   clap_param_rescan_flags flags) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    self->bridge_.send_main_thread_message(clap::ext::params::host::Rescan{
        .owner_instance_id = self->owner_instance_id(), .flags = flags});
}

void CLAP_ABI clap_host_proxy::ext_params_clear(const clap_host_t* host,
                                                clap_id param_id,
                                                clap_param_clear_flags flags) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    self->bridge_.send_main_thread_message(clap::ext::params::host::Clear{
        .owner_instance_id = self->owner_instance_id(),
        .param_id = param_id,
        .flags = flags});
}

void CLAP_ABI
clap_host_proxy::ext_params_request_flush(const clap_host_t* host) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    self->bridge_.send_audio_thread_message(
        clap::ext::params::host::RequestFlush{.owner_instance_id =
                                                  self->owner_instance_id()});
}

void CLAP_ABI clap_host_proxy::ext_state_mark_dirty(const clap_host_t* host) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    self->bridge_.send_main_thread_message(clap::ext::state::host::MarkDirty{
        .owner_instance_id = self->owner_instance_id()});
}

void CLAP_ABI clap_host_proxy::ext_tail_changed(const clap_host_t* host) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    self->bridge_.send_audio_thread_message(clap::ext::tail::host::Changed{
        .owner_instance_id = self->owner_instance_id()});
}
