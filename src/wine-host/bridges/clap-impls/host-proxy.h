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

#include <atomic>

#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>
#include <clap/host.h>

#include "../../common/serialization/clap/plugin-factory.h"

// Forward declaration to avoid circular includes
class ClapBridge;

/**
 * A proxy for a plugin's `clap_host`.
 *
 * Because the plugin may not query host extensions until `init()` is called,
 * the available host extensions will only be populated at that point.
 */
class clap_host_proxy {
   public:
    /**
     * Construct a host proxy based for a plugin. The available extensions will
     * be populated when the host calls `clap_plugin::init()` as mentioned
     * above.
     */
    clap_host_proxy(ClapBridge& bridge,
                    size_t owner_instance_id,
                    clap::host::Host host_args);

    clap_host_proxy(const clap_host_proxy&) = delete;
    clap_host_proxy& operator=(const clap_host_proxy&) = delete;
    clap_host_proxy(clap_host_proxy&&) = delete;
    clap_host_proxy& operator=(clap_host_proxy&&) = delete;

    /**
     * Get a `clap_host` vtable that can be passed to the plugin.
     */
    inline const clap_host_t* host_vtable() const { return &host_vtable_; }
    /**
     * The instance ID of the plugin instance this proxy belongs to.
     */
    inline size_t owner_instance_id() const { return owner_instance_id_; }

    static const void* CLAP_ABI host_get_extension(const struct clap_host* host,
                                                   const char* extension_id);
    static void CLAP_ABI host_request_restart(const struct clap_host* host);
    static void CLAP_ABI host_request_process(const struct clap_host* host);
    static void CLAP_ABI host_request_callback(const struct clap_host* host);

    static bool CLAP_ABI
    ext_audio_ports_is_rescan_flag_supported(const clap_host_t* host,
                                             uint32_t flag);
    static void CLAP_ABI ext_audio_ports_rescan(const clap_host_t* host,
                                                uint32_t flags);

    static uint32_t CLAP_ABI
    ext_note_ports_supported_dialects(const clap_host_t* host);
    static void CLAP_ABI ext_note_ports_rescan(const clap_host_t* host,
                                               uint32_t flags);

    /**
     * The extensions supported by the host, set just before calling
     * `clap_plugin::init()` on the bridged plugin. We'll allow the plugin to
     * query these extensions through `clap_host::get_extension()`.
     */
    clap::host::SupportedHostExtensions supported_extensions_;

   private:
    ClapBridge& bridge_;
    size_t owner_instance_id_;
    clap::host::Host host_args_;

    /**
     * The vtable for `clap_host`, requires that this object is never moved or
     * copied. We'll use the host data pointer instead of placing this vtable at
     * the start of the struct and directly casting the `clap_host_t*`.
     */
    const clap_host_t host_vtable_;

    // Extensions also have vtables. Whether or not we expose these to the host
    // depends on whether the plugin supported this extension when the host
    // called `clap_plugin::init()`.
    const clap_host_audio_ports ext_audio_ports_vtable;
    const clap_host_note_ports ext_note_ports_vtable;

    /**
     * Keeps track of whether there are pending host callbacks. Used to prevent
     * calling `clap_plugin::on_main_thread()` multiple times in a row when the
     * plugin calls `clap_host::request_callback()` multiple times before
     * `clap_plugin::on_main_thread()` is called.
     */
    std::atomic_bool has_pending_host_callbacks_ = false;
};
