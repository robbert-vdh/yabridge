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
      gui(static_cast<const clap_host_gui_t*>(
          host.get_extension(&host, CLAP_EXT_GUI))),
      latency(static_cast<const clap_host_latency_t*>(
          host.get_extension(&host, CLAP_EXT_LATENCY))),
      note_ports(static_cast<const clap_host_note_ports_t*>(
          host.get_extension(&host, CLAP_EXT_NOTE_PORTS))),
      params(static_cast<const clap_host_params_t*>(
          host.get_extension(&host, CLAP_EXT_PARAMS))),
      state(static_cast<const clap_host_state_t*>(
          host.get_extension(&host, CLAP_EXT_STATE))),
      tail(static_cast<const clap_host_tail_t*>(
          host.get_extension(&host, CLAP_EXT_TAIL))) {}

ClapHostExtensions::ClapHostExtensions() noexcept {}

clap::host::SupportedHostExtensions ClapHostExtensions::supported()
    const noexcept {
    return clap::host::SupportedHostExtensions{
        .supports_audio_ports = audio_ports != nullptr,
        .supports_gui = gui != nullptr,
        .supports_latency = latency != nullptr,
        .supports_note_ports = note_ports != nullptr,
        .supports_params = params != nullptr,
        .supports_state = state != nullptr,
        .supports_tail = tail != nullptr};
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
      ext_gui_vtable(clap_plugin_gui_t{
          .is_api_supported = ext_gui_is_api_supported,
          .get_preferred_api = ext_gui_get_preferred_api,
          .create = ext_gui_create,
          .destroy = ext_gui_destroy,
          .set_scale = ext_gui_set_scale,
          .get_size = ext_gui_get_size,
          .can_resize = ext_gui_can_resize,
          .get_resize_hints = ext_gui_get_resize_hints,
          .adjust_size = ext_gui_adjust_size,
          .set_size = ext_gui_set_size,
          .set_parent = ext_gui_set_parent,
          .set_transient = ext_gui_set_transient,
          .suggest_title = ext_gui_suggest_title,
          .show = ext_gui_show,
          .hide = ext_gui_hide,
      }),
      ext_latency_vtable(clap_plugin_latency_t{
          .get = ext_latency_get,
      }),
      ext_note_ports_vtable(clap_plugin_note_ports_t{
          .count = ext_note_ports_count,
          .get = ext_note_ports_get,
      }),
      ext_params_vtable(clap_plugin_params_t{
          .count = ext_params_count,
          .get_info = ext_params_get_info,
          .get_value = ext_params_get_value,
          .value_to_text = ext_params_value_to_text,
          .text_to_value = ext_params_text_to_value,
          .flush = ext_params_flush,
      }),
      ext_state_vtable(clap_plugin_state_t{
          .save = ext_state_save,
          .load = ext_state_load,
      }),
      ext_tail_vtable(clap_plugin_tail_t{
          .get = ext_tail_get,
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
    self->host_extensions_ = ClapHostExtensions(*self->host_);

    const clap::plugin::InitResponse response =
        self->bridge_.send_main_thread_message(clap::plugin::Init{
            .instance_id = self->instance_id(),
            .supported_host_extensions = self->host_extensions_.supported()});

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

    const void* extension_ptr = nullptr;
    if (self->supported_extensions_.supports_audio_ports &&
        strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
        extension_ptr = &self->ext_audio_ports_vtable;
    } else if (self->supported_extensions_.supports_gui &&
               strcmp(id, CLAP_EXT_GUI) == 0) {
        extension_ptr = &self->ext_gui_vtable;
    } else if (self->supported_extensions_.supports_latency &&
               strcmp(id, CLAP_EXT_LATENCY) == 0) {
        extension_ptr = &self->ext_latency_vtable;
    } else if (self->supported_extensions_.supports_note_ports &&
               strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) {
        extension_ptr = &self->ext_note_ports_vtable;
    } else if (self->supported_extensions_.supports_params &&
               strcmp(id, CLAP_EXT_PARAMS) == 0) {
        extension_ptr = &self->ext_params_vtable;
    } else if (self->supported_extensions_.supports_state &&
               strcmp(id, CLAP_EXT_STATE) == 0) {
        extension_ptr = &self->ext_state_vtable;
    } else if (self->supported_extensions_.supports_tail &&
               strcmp(id, CLAP_EXT_TAIL) == 0) {
        extension_ptr = &self->ext_tail_vtable;
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

bool CLAP_ABI
clap_plugin_proxy::ext_gui_is_api_supported(const clap_plugin_t* plugin,
                                            const char* api,
                                            bool is_floating) {
    assert(plugin && plugin->plugin_data && api);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    // We only support embedded X11 windows for now
    if (strcmp(api, CLAP_WINDOW_API_X11) != 0 || is_floating) {
        return false;
    }

    return self->bridge_.send_main_thread_message(
        clap::ext::gui::plugin::IsApiSupported{
            .instance_id = self->instance_id(),
            // This will be translated to WIN32 on the Wine plugin host side
            .api = clap::ext::gui::ApiType::X11,
            .is_floating = is_floating});
}

bool CLAP_ABI
clap_plugin_proxy::ext_gui_get_preferred_api(const clap_plugin_t* plugin,
                                             const char** api,
                                             bool* is_floating) {
    assert(plugin && plugin->plugin_data && api && is_floating);

    // We only support floating X11 windows right now
    *api = CLAP_WINDOW_API_X11;
    *is_floating = false;

    return true;
}

bool CLAP_ABI clap_plugin_proxy::ext_gui_create(const clap_plugin_t* plugin,
                                                const char* api,
                                                bool is_floating) {
    assert(plugin && plugin->plugin_data && api);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    // We only support embedded X11 windows for now
    if (strcmp(api, CLAP_WINDOW_API_X11) != 0 || is_floating) {
        return false;
    }

    return self->bridge_.send_main_thread_message(
        clap::ext::gui::plugin::Create{
            .instance_id = self->instance_id(),
            // This will be translated to WIN32 on the Wine plugin host side
            .api = clap::ext::gui::ApiType::X11,
            .is_floating = is_floating});
}

void CLAP_ABI clap_plugin_proxy::ext_gui_destroy(const clap_plugin_t* plugin) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    self->bridge_.send_main_thread_message(
        clap::ext::gui::plugin::Destroy{.instance_id = self->instance_id()});
}

bool CLAP_ABI clap_plugin_proxy::ext_gui_set_scale(const clap_plugin_t* plugin,
                                                   double scale) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    return self->bridge_.send_main_thread_message(
        clap::ext::gui::plugin::SetScale{.instance_id = self->instance_id(),
                                         .scale = scale});
}

bool CLAP_ABI clap_plugin_proxy::ext_gui_get_size(const clap_plugin_t* plugin,
                                                  uint32_t* width,
                                                  uint32_t* height) {
    assert(plugin && plugin->plugin_data && width && height);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    const clap::ext::gui::plugin::GetSizeResponse response =
        self->bridge_.send_main_thread_message(clap::ext::gui::plugin::GetSize{
            .instance_id = self->instance_id()});

    if (response.result) {
        *width = response.width;
        *height = response.height;
    }

    return response.result;
}

bool CLAP_ABI
clap_plugin_proxy::ext_gui_can_resize(const clap_plugin_t* plugin) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    return self->bridge_.send_main_thread_message(
        clap::ext::gui::plugin::CanResize{.instance_id = self->instance_id()});
}

bool CLAP_ABI
clap_plugin_proxy::ext_gui_get_resize_hints(const clap_plugin_t* plugin,
                                            clap_gui_resize_hints_t* hints) {
    assert(plugin && plugin->plugin_data && hints);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    const clap::ext::gui::plugin::GetResizeHintsResponse response =
        self->bridge_.send_main_thread_message(
            clap::ext::gui::plugin::GetResizeHints{.instance_id =
                                                       self->instance_id()});
    if (response.result) {
        *hints = *response.result;

        return true;
    } else {
        return false;
    }
}

bool CLAP_ABI
clap_plugin_proxy::ext_gui_adjust_size(const clap_plugin_t* plugin,
                                       uint32_t* width,
                                       uint32_t* height) {
    assert(plugin && plugin->plugin_data && width && height);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    const clap::ext::gui::plugin::AdjustSizeResponse response =
        self->bridge_.send_main_thread_message(
            clap::ext::gui::plugin::AdjustSize{
                .instance_id = self->instance_id(),
                .width = *width,
                .height = *height});

    if (response.result) {
        *width = response.updated_width;
        *height = response.updated_height;
    }

    return response.result;
}

bool CLAP_ABI clap_plugin_proxy::ext_gui_set_size(const clap_plugin_t* plugin,
                                                  uint32_t width,
                                                  uint32_t height) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    return self->bridge_.send_main_thread_message(
        clap::ext::gui::plugin::SetSize{.instance_id = self->instance_id(),
                                        .width = width,
                                        .height = height});
}

bool CLAP_ABI
clap_plugin_proxy::ext_gui_set_parent(const clap_plugin_t* plugin,
                                      const clap_window_t* window) {
    assert(plugin && plugin->plugin_data && window);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    // We only support X11 windows right now, so this will always be an X11
    // window
    return self->bridge_.send_main_thread_message(
        clap::ext::gui::plugin::SetParent{.instance_id = self->instance_id(),
                                          .x11_window = window->x11});
}

bool CLAP_ABI
clap_plugin_proxy::ext_gui_set_transient(const clap_plugin_t* plugin,
                                         const clap_window_t* window) {
    assert(plugin && plugin->plugin_data && window);

    // We don't support floating windows right now
    return false;
}

void CLAP_ABI
clap_plugin_proxy::ext_gui_suggest_title(const clap_plugin_t* plugin,
                                         const char* title) {
    assert(plugin && plugin->plugin_data && title);

    // We don't support floating windows right now
}

bool CLAP_ABI clap_plugin_proxy::ext_gui_show(const clap_plugin_t* plugin) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    return self->bridge_.send_main_thread_message(
        clap::ext::gui::plugin::Show{.instance_id = self->instance_id()});
}

bool CLAP_ABI clap_plugin_proxy::ext_gui_hide(const clap_plugin_t* plugin) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    return self->bridge_.send_main_thread_message(
        clap::ext::gui::plugin::Hide{.instance_id = self->instance_id()});
}

uint32_t CLAP_ABI
clap_plugin_proxy::ext_latency_get(const clap_plugin_t* plugin) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    return self->bridge_.send_main_thread_message(
        clap::ext::latency::plugin::Get{.instance_id = self->instance_id()});
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

uint32_t CLAP_ABI
clap_plugin_proxy::ext_params_count(const clap_plugin_t* plugin) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    return self->bridge_.send_main_thread_message(
        clap::ext::params::plugin::Count{.instance_id = self->instance_id()});
}

bool CLAP_ABI
clap_plugin_proxy::ext_params_get_info(const clap_plugin_t* plugin,
                                       uint32_t param_index,
                                       clap_param_info_t* param_info) {
    assert(plugin && plugin->plugin_data && param_info);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    const clap::ext::params::plugin::GetInfoResponse response =
        self->bridge_.send_main_thread_message(
            clap::ext::params::plugin::GetInfo{
                .instance_id = self->instance_id(),
                .param_index = param_index});
    if (response.result) {
        response.result->reconstruct(*param_info);

        return true;
    } else {
        return false;
    }
}

bool CLAP_ABI
clap_plugin_proxy::ext_params_get_value(const clap_plugin_t* plugin,
                                        clap_id param_id,
                                        double* value) {
    assert(plugin && plugin->plugin_data && value);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    const clap::ext::params::plugin::GetValueResponse response =
        self->bridge_.send_main_thread_message(
            clap::ext::params::plugin::GetValue{
                .instance_id = self->instance_id(), .param_id = param_id});
    if (response.result) {
        *value = *response.result;

        return true;
    } else {
        return false;
    }
}

bool CLAP_ABI
clap_plugin_proxy::ext_params_value_to_text(const clap_plugin_t* plugin,
                                            clap_id param_id,
                                            double value,
                                            char* display,
                                            uint32_t size) {
    assert(plugin && plugin->plugin_data && display);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    const clap::ext::params::plugin::ValueToTextResponse response =
        self->bridge_.send_main_thread_message(
            clap::ext::params::plugin::ValueToText{
                .instance_id = self->instance_id(),
                .param_id = param_id,
                .value = value});
    if (response.result) {
        strlcpy_buffer(display, *response.result, size);

        return true;
    } else {
        return false;
    }
}

bool CLAP_ABI
clap_plugin_proxy::ext_params_text_to_value(const clap_plugin_t* plugin,
                                            clap_id param_id,
                                            const char* display,
                                            double* value) {
    assert(plugin && plugin->plugin_data && display && value);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    const clap::ext::params::plugin::TextToValueResponse response =
        self->bridge_.send_main_thread_message(
            clap::ext::params::plugin::TextToValue{
                .instance_id = self->instance_id(),
                .param_id = param_id,
                .display = display});
    if (response.result) {
        *value = *response.result;

        return true;
    } else {
        return false;
    }
}

void CLAP_ABI
clap_plugin_proxy::ext_params_flush(const clap_plugin_t* plugin,
                                    const clap_input_events_t* in,
                                    const clap_output_events_t* out) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    // This may also be called on the audio thread and it is never called during
    // process, so always using the audio thread here is safe
    self->bridge_.send_audio_thread_message(
        clap::ext::params::plugin::Flush{.instance_id = self->instance_id()});
}

bool CLAP_ABI clap_plugin_proxy::ext_state_save(const clap_plugin_t* plugin,
                                                const clap_ostream_t* stream) {
    assert(plugin && plugin->plugin_data && stream);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    const clap::ext::state::plugin::SaveResponse response =
        self->bridge_.send_main_thread_message(
            clap::ext::state::plugin::Save{.instance_id = self->instance_id()});
    if (response.result) {
        response.result->write_to_stream(*stream);

        return true;
    } else {
        return false;
    }
}

bool CLAP_ABI clap_plugin_proxy::ext_state_load(const clap_plugin_t* plugin,
                                                const clap_istream_t* stream) {
    assert(plugin && plugin->plugin_data && stream);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    return self->bridge_.send_main_thread_message(
        clap::ext::state::plugin::Load{.instance_id = self->instance_id(),
                                       .stream = *stream});
}

uint32_t CLAP_ABI clap_plugin_proxy::ext_tail_get(const clap_plugin_t* plugin) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    return self->bridge_.send_audio_thread_message(
        clap::ext::tail::plugin::Get{.instance_id = self->instance_id()});
}
