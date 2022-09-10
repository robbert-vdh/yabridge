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

#pragma once

#include <vector>

#include <clap/plugin.h>

#include "../../common/serialization/clap/plugin.h"

// Forward declaration to avoid circular includes
class ClapPluginBridge;

/**
 * A proxy for a `clap_plugin`.
 */
class clap_plugin_proxy {
   public:
    /**
     * Construct a proxy for a plugin that has already been created on the Wine
     * side. This is done in our `clap_plugin_factory::create()` implementation.
     * The instance ID lets us link calls the host makes on a plugin object to a
     * Windows CLAP plugin running under the Wine plugin host.
     */
    clap_plugin_proxy(ClapPluginBridge& bridge,
                      size_t instance_id,
                      clap::plugin::Descriptor descriptor,
                      const clap_host_t* host);

    clap_plugin_proxy(const clap_plugin_proxy&) = delete;
    clap_plugin_proxy& operator=(const clap_plugin_proxy&) = delete;
    clap_plugin_proxy(clap_plugin_proxy&&) = delete;
    clap_plugin_proxy& operator=(clap_plugin_proxy&&) = delete;

    /**
     * Get a `clap_plugin` vtable that can be passed to the host when creating a
     * plugin instance.
     */
    inline const clap_plugin_t* plugin_vtable() const {
        return &plugin_vtable_;
    }
    /**
     * The instance ID of the plugin instance this proxy belongs to.
     */
    inline size_t instance_id() const { return instance_id_; }

    static bool CLAP_ABI plugin_init(const struct clap_plugin* plugin);
    static void CLAP_ABI plugin_destroy(const struct clap_plugin* plugin);
    static bool CLAP_ABI plugin_activate(const struct clap_plugin* plugin,
                                         double sample_rate,
                                         uint32_t min_frames_count,
                                         uint32_t max_frames_count);
    static void CLAP_ABI plugin_deactivate(const struct clap_plugin* plugin);
    static bool CLAP_ABI
    plugin_start_processing(const struct clap_plugin* plugin);
    static void CLAP_ABI
    plugin_stop_processing(const struct clap_plugin* plugin);
    static void CLAP_ABI plugin_reset(const struct clap_plugin* plugin);
    static clap_process_status CLAP_ABI
    plugin_process(const struct clap_plugin* plugin,
                   const clap_process_t* process);
    static const void* CLAP_ABI
    plugin_get_extension(const struct clap_plugin* plugin, const char* id);
    static void CLAP_ABI
    plugin_on_main_thread(const struct clap_plugin* plugin);

   private:
    ClapPluginBridge& bridge_;
    size_t instance_id_;
    clap::plugin::Descriptor descriptor_;

    /**
     * The vtable for `clap_plugin`, requires that this object is never moved or
     * copied. We'll use the host data pointer instead of placing this vtable at
     * the start of the struct and directly casting the `clap_plugin_t*`.
     */
    const clap_plugin_t plugin_vtable_;

    /**
     * The extensions supported by the bridged plugin. Set after a successful
     * `clap_plugin::init()` call. We'll allow the host to query these same
     * extensions from our plugin proxy.
     */
    clap::plugin::SupportedPluginExtensions supported_extensions_;

    /**
     * The `clap_host_t*` passed when creating the instance. Any callbacks made
     * by the proxied plugin instance must go through ere.
     */
    const clap_host_t* host_;
};
