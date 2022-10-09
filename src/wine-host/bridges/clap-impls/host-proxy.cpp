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

// NOTE: The liberal use of `send_mutually_recursive_main_thread_message()` here
//       is because otherwise it's very easy to run into a deadlock when both
//       sides use `clap_host::request_callback()`+`clap_plugin::on_main_thread`
//       at the same time

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
      ext_gui_vtable(clap_host_gui_t{
          .resize_hints_changed = ext_gui_resize_hints_changed,
          .request_resize = ext_gui_request_resize,
          .request_show = ext_gui_request_show,
          .request_hide = ext_gui_request_hide,
          .closed = ext_gui_closed,
      }),
      ext_latency_vtable(clap_host_latency_t{
          .changed = ext_latency_changed,
      }),
      ext_log_vtable(clap_host_log_t{
          .log = ext_log_log,
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
      }),
      ext_thread_check_vtable(clap_host_thread_check_t{
          .is_main_thread = ext_thread_check_is_main_thread,
          .is_audio_thread = ext_thread_check_is_audio_thread,
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
    } else if (self->supported_extensions_.supports_gui &&
               strcmp(extension_id, CLAP_EXT_GUI) == 0) {
        extension_ptr = &self->ext_gui_vtable;
    } else if (self->supported_extensions_.supports_latency &&
               strcmp(extension_id, CLAP_EXT_LATENCY) == 0) {
        extension_ptr = &self->ext_latency_vtable;
    } else if (strcmp(extension_id, CLAP_EXT_LOG) == 0) {
        // This extension is always supported. We'll bridge it if the host also
        // supports it, or we'll print the message if it doesn't. That allows us
        // to filter misbehavior messages.
        extension_ptr = &self->ext_log_vtable;
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
    } else if (strcmp(extension_id, CLAP_EXT_THREAD_CHECK) == 0) {
        // This extension doesn't require any bridging
        extension_ptr = &self->ext_thread_check_vtable;
    }

    self->bridge_.logger_.log_extension_query("clap_host::get_extension",
                                              extension_ptr, extension_id);

    return extension_ptr;
}

void CLAP_ABI
clap_host_proxy::host_request_restart(const struct clap_host* host) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    self->bridge_.send_mutually_recursive_main_thread_message(
        clap::host::RequestRestart{.owner_instance_id =
                                       self->owner_instance_id()});
}

void CLAP_ABI
clap_host_proxy::host_request_process(const struct clap_host* host) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    self->bridge_.send_mutually_recursive_main_thread_message(
        clap::host::RequestProcess{.owner_instance_id =
                                       self->owner_instance_id()});
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

    return self->bridge_.send_mutually_recursive_main_thread_message(
        clap::ext::audio_ports::host::IsRescanFlagSupported{
            .owner_instance_id = self->owner_instance_id(), .flag = flag});
}

void CLAP_ABI clap_host_proxy::ext_audio_ports_rescan(const clap_host_t* host,
                                                      uint32_t flags) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    self->bridge_.send_mutually_recursive_main_thread_message(
        clap::ext::audio_ports::host::Rescan{
            .owner_instance_id = self->owner_instance_id(), .flags = flags});
}

void CLAP_ABI
clap_host_proxy::ext_gui_resize_hints_changed(const clap_host_t* host) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    self->bridge_.send_mutually_recursive_main_thread_message(
        clap::ext::gui::host::ResizeHintsChanged{
            .owner_instance_id = self->owner_instance_id()});
}

bool CLAP_ABI clap_host_proxy::ext_gui_request_resize(const clap_host_t* host,
                                                      uint32_t width,
                                                      uint32_t height) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    // HACK: Surge XT/the CLAP JUCE Extensions get stuck in a resize loop when
    //       the host tries to resize the window. It will send
    //       `clap_host_gui::request_resize()` in response to
    //       `clap_plugin_gui::set_size()` with the same size it has just set.
    //       We'll need to filter these calls out to prevent this from causing
    //       issues.
    if (const std::optional<Size> current_size =
            self->bridge_.editor_size(self->owner_instance_id());
        current_size && current_size->width == width &&
        current_size->height == height) {
        return true;
    }

    const bool result =
        self->bridge_.send_mutually_recursive_main_thread_message(
            clap::ext::gui::host::RequestResize{
                .owner_instance_id = self->owner_instance_id(),
                .width = width,
                .height = height});

    // If the resize request was accepted by the host, then we'll also resize
    // our editor window
    if (result) {
        self->bridge_.maybe_resize_editor(self->owner_instance_id_, width,
                                          height);
    }

    return result;
}

bool CLAP_ABI clap_host_proxy::ext_gui_request_show(const clap_host_t* host) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    return self->bridge_.send_mutually_recursive_main_thread_message(
        clap::ext::gui::host::RequestShow{.owner_instance_id =
                                              self->owner_instance_id()});
}

bool CLAP_ABI clap_host_proxy::ext_gui_request_hide(const clap_host_t* host) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    return self->bridge_.send_mutually_recursive_main_thread_message(
        clap::ext::gui::host::RequestHide{.owner_instance_id =
                                              self->owner_instance_id()});
}

void CLAP_ABI clap_host_proxy::ext_gui_closed(const clap_host_t* host,
                                              bool was_destroyed) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    self->bridge_.send_mutually_recursive_main_thread_message(
        clap::ext::gui::host::Closed{
            .owner_instance_id = self->owner_instance_id(),
            .was_destroyed = was_destroyed});
}

void CLAP_ABI clap_host_proxy::ext_latency_changed(const clap_host_t* host) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    self->bridge_.send_mutually_recursive_main_thread_message(
        clap::ext::latency::host::Changed{.owner_instance_id =
                                              self->owner_instance_id()});
}

void CLAP_ABI clap_host_proxy::ext_log_log(const clap_host_t* host,
                                           clap_log_severity severity,
                                           const char* msg) {
    assert(host && host->host_data && msg);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    // We'll always support this extension, even if the host doesn't. That
    // allows us to filter misbehavior messages from the CLAP helper.
    if ((severity == CLAP_LOG_HOST_MISBEHAVING ||
         severity == CLAP_LOG_PLUGIN_MISBEHAVING) &&
        self->bridge_.logger_.verbosity() < Logger::Verbosity::all_events) {
        return;
    }

    // We'll bridge this if possible, otherwise we'll just print the message to
    // the logger (through STDERR)
    if (self->supported_extensions_.supports_log) {
        self->bridge_.send_audio_thread_message(clap::ext::log::host::Log{
            .owner_instance_id = self->owner_instance_id(),
            .severity = severity,
            .msg = msg});
    } else {
        switch (severity) {
            case CLAP_LOG_DEBUG:
                std::cerr << "[DEBUG] ";
                break;
            case CLAP_LOG_INFO:
                std::cerr << "[INFO] ";
                break;
            case CLAP_LOG_WARNING:
                std::cerr << "[WARNING] ";
                break;
            case CLAP_LOG_ERROR:
                std::cerr << "[ERROR] ";
                break;
            case CLAP_LOG_FATAL:
                std::cerr << "[FATAL] ";
                break;
            case CLAP_LOG_HOST_MISBEHAVING:
                std::cerr << "[HOST_MISBEHAVING] ";
                break;
            case CLAP_LOG_PLUGIN_MISBEHAVING:
                std::cerr << "[PLUGIN_MISBEHAVING] ";
                break;
            default:
                std::cerr << "[unknown log level " << severity << "] ";
                break;
        }

        std::cerr << msg << std::endl;
    }
}

uint32_t CLAP_ABI
clap_host_proxy::ext_note_ports_supported_dialects(const clap_host_t* host) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    return self->bridge_.send_mutually_recursive_main_thread_message(
        clap::ext::note_ports::host::SupportedDialects{
            .owner_instance_id = self->owner_instance_id()});
}

void CLAP_ABI clap_host_proxy::ext_note_ports_rescan(const clap_host_t* host,
                                                     uint32_t flags) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    self->bridge_.send_mutually_recursive_main_thread_message(
        clap::ext::note_ports::host::Rescan{
            .owner_instance_id = self->owner_instance_id(), .flags = flags});
}

void CLAP_ABI
clap_host_proxy::ext_params_rescan(const clap_host_t* host,
                                   clap_param_rescan_flags flags) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    // NOTE: This one in particular needs the mutual recursion because Surge XT
    //       calls this function immediately when inserting, and when the host
    //       opens the GUI at the same time this would otherwise deadlock
    self->bridge_.send_mutually_recursive_main_thread_message(
        clap::ext::params::host::Rescan{
            .owner_instance_id = self->owner_instance_id(), .flags = flags});
}

void CLAP_ABI clap_host_proxy::ext_params_clear(const clap_host_t* host,
                                                clap_id param_id,
                                                clap_param_clear_flags flags) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    self->bridge_.send_mutually_recursive_main_thread_message(
        clap::ext::params::host::Clear{
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

    self->bridge_.send_mutually_recursive_main_thread_message(
        clap::ext::state::host::MarkDirty{.owner_instance_id =
                                              self->owner_instance_id()});
}

void CLAP_ABI clap_host_proxy::ext_tail_changed(const clap_host_t* host) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    self->bridge_.send_audio_thread_message(clap::ext::tail::host::Changed{
        .owner_instance_id = self->owner_instance_id()});
}

bool CLAP_ABI
clap_host_proxy::ext_thread_check_is_main_thread(const clap_host_t* host) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    return self->bridge_.main_context_.is_gui_thread();
}

bool CLAP_ABI
clap_host_proxy::ext_thread_check_is_audio_thread(const clap_host_t* host) {
    assert(host && host->host_data);
    auto self = static_cast<const clap_host_proxy*>(host->host_data);

    // We don't keep track of audio threads, but as long as the plugin doesn't
    // do audio thread stuff on the GUI thread everything's fine
    return !self->bridge_.main_context_.is_gui_thread();
}
