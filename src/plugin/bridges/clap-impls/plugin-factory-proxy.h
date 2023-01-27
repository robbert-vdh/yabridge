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

#pragma once

#include <vector>

#include <clap/factory/plugin-factory.h>

#include "../../common/serialization/clap/plugin.h"

// Forward declaration to avoid circular includes
class ClapPluginBridge;

/**
 * A proxy for a plugin's `clap_plugin_factory`.
 */
class clap_plugin_factory_proxy {
   public:
    /**
     * The vtable for `clap_plugin_factory`, requires that this object is never
     * moved or copied. This is positioned at the start of the struct so we can
     * cast between them (with only a bit of UB).
     *
     * NOTE: CLAP does not provide a user pointer field for this vtable like it
     *       does with other types because it expects the factory to be a
     *       statically initialized singleton. That's why we need to do this
     *       cast instead.
     */
    const clap_plugin_factory_t plugin_factory_vtable;

    /**
     * Construct the plugin factory proxy based on the plugin descriptors
     * retrieved from a `clap::factory::plugin_factory::ListReponse`.
     */
    clap_plugin_factory_proxy(
        ClapPluginBridge& bridge,
        std::vector<clap::plugin::Descriptor> descriptors);

    clap_plugin_factory_proxy(const clap_plugin_factory_proxy&) = delete;
    clap_plugin_factory_proxy& operator=(const clap_plugin_factory_proxy&) =
        delete;
    clap_plugin_factory_proxy(clap_plugin_factory_proxy&&) = delete;
    clap_plugin_factory_proxy& operator=(clap_plugin_factory_proxy&&) = delete;

    static uint32_t CLAP_ABI
    plugin_factory_get_plugin_count(const struct clap_plugin_factory* factory);
    static const clap_plugin_descriptor_t* CLAP_ABI
    plugin_factory_get_plugin_descriptor(
        const struct clap_plugin_factory* factory,
        uint32_t index);
    static const clap_plugin_t* CLAP_ABI
    plugin_factory_create_plugin(const struct clap_plugin_factory* factory,
                                 const clap_host_t* host,
                                 const char* plugin_id);

   private:
    ClapPluginBridge& bridge_;

    std::vector<clap::plugin::Descriptor> descriptors_;
};
