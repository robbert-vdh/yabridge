// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2023 Robbert van der Helm
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

#include "plugin-factory-proxy.h"

#include "../clap.h"

clap_plugin_factory_proxy::clap_plugin_factory_proxy(
    ClapPluginBridge& bridge,
    std::vector<clap::plugin::Descriptor> descriptors)
    : plugin_factory_vtable(clap_plugin_factory_t{
          .get_plugin_count = plugin_factory_get_plugin_count,
          .get_plugin_descriptor = plugin_factory_get_plugin_descriptor,
          .create_plugin = plugin_factory_create_plugin,
      }),
      bridge_(bridge),
      descriptors_(std::move(descriptors)) {}

uint32_t CLAP_ABI clap_plugin_factory_proxy::plugin_factory_get_plugin_count(
    const struct clap_plugin_factory* factory) {
    assert(factory);
    auto self = reinterpret_cast<const clap_plugin_factory_proxy*>(factory);

    return self->descriptors_.size();
}

const clap_plugin_descriptor_t* CLAP_ABI
clap_plugin_factory_proxy::plugin_factory_get_plugin_descriptor(
    const struct clap_plugin_factory* factory,
    uint32_t index) {
    assert(factory);
    auto self = reinterpret_cast<const clap_plugin_factory_proxy*>(factory);

    if (index < self->descriptors_.size()) {
        return self->descriptors_[index].get();
    } else {
        return nullptr;
    }
}

const clap_plugin_t* CLAP_ABI
clap_plugin_factory_proxy::plugin_factory_create_plugin(
    const struct clap_plugin_factory* factory,
    const clap_host_t* host,
    const char* plugin_id) {
    assert(factory && host && plugin_id);
    auto self = reinterpret_cast<const clap_plugin_factory_proxy*>(factory);

    // We'll need to store another copy of this descriptor in the plugin
    // instance
    const auto descriptor =
        std::find_if(self->descriptors_.begin(), self->descriptors_.end(),
                     [plugin_id](const auto& descriptor) {
                         return descriptor.id == plugin_id;
                     });
    if (descriptor == self->descriptors_.end()) {
        self->bridge_.logger_.log_trace([&]() {
            return "The host tried to create an instance for ID \"" +
                   std::string(plugin_id) +
                   "\", but we don't have a descriptor for this plugin.";
        });

        return nullptr;
    }

    const clap::plugin_factory::CreateResponse response =
        self->bridge_.send_main_thread_message(clap::plugin_factory::Create{
            .host = *host, .plugin_id = plugin_id});
    if (response.instance_id) {
        // This plugin proxy is tied to the instance ID created on the Wine
        // side. That way we can link function calls from the host to the
        // correct plugin instance, and callbacks made from the plugin to the
        // correct host instance.
        self->bridge_.register_plugin_proxy(std::make_unique<clap_plugin_proxy>(
            self->bridge_, *response.instance_id, *descriptor, host));

        const auto& [plugin_proxy, _] =
            self->bridge_.get_proxy(*response.instance_id);

        return plugin_proxy.plugin_vtable();
    } else {
        // The plugin couldn't be created, for whatever reason that might be
        return nullptr;
    }
}
