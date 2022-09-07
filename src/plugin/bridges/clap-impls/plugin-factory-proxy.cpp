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
      descriptors_(std::move(descriptors)){};

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
    // TODO: Implement
    return nullptr;
}
