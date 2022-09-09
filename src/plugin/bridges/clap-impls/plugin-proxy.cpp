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
                                     clap::plugin::Descriptor descriptor)
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
      }) {}

bool CLAP_ABI clap_plugin_proxy::plugin_init(const struct clap_plugin* plugin) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    // TODO: Implement
    return false;
}

void CLAP_ABI
clap_plugin_proxy::plugin_destroy(const struct clap_plugin* plugin) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    // TODO: Destroy on the Wine side

    // This deallocates and destroys `self`
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

    // TODO: Implement
    return false;
}

void CLAP_ABI
clap_plugin_proxy::plugin_deactivate(const struct clap_plugin* plugin) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    // TODO: Implement
}

bool CLAP_ABI
clap_plugin_proxy::plugin_start_processing(const struct clap_plugin* plugin) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    // TODO: Implement
    return false;
}

void CLAP_ABI
clap_plugin_proxy::plugin_stop_processing(const struct clap_plugin* plugin) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    // TODO: Implement
}

void CLAP_ABI
clap_plugin_proxy::plugin_reset(const struct clap_plugin* plugin) {
    assert(plugin && plugin->plugin_data);
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    // TODO: Implement
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
    auto self = static_cast<const clap_plugin_proxy*>(plugin->plugin_data);

    // TODO: Use this for spooling main thread callbacks
}
