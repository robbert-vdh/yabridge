// yabridge: a Wine VST bridge
// Copyright (C) 2020  Robbert van der Helm
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

#include <bitsery/traits/string.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "../plugins.h"

// The plugin should always be compiled to a 64-bit version, but the host
// application can also be 32-bit to allow using 32-bit legacy Windows VST in a
// modern Linux VST host. Because of this we have to make sure to always use
// 64-bit integers in places where we would otherwise use `size_t` and
// `intptr_t`. Otherwise the binary serialization would break. The 64 <-> 32 bit
// conversion for the 32-bit host application won't cause any issues for us
// since we can't directly pass pointers between the plugin and the host anyway.

#ifndef __WINE__
// Sanity check for the plugin, both the 64 and 32 bit hosts should follow these
// conventions
static_assert(std::is_same_v<size_t, uint64_t>);
static_assert(std::is_same_v<intptr_t, int64_t>);
#endif
using native_size_t = uint64_t;
using native_intptr_t = int64_t;

// The cannonical overloading template for `std::visitor`, not sure why this
// isn't part of the standard library
template <class... Ts>
struct overload : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
overload(Ts...) -> overload<Ts...>;

/**
 * An object containing the startup options for hosting a plugin. These options
 * are passed to `yabridge-host.exe` as command line arguments, and they are
 * used directly by group host processes.
 */
struct HostRequest {
    PluginType plugin_type;
    std::string plugin_path;
    std::string endpoint_base_dir;

    template <typename S>
    void serialize(S& s) {
        s.object(plugin_type);
        s.text1b(plugin_path, 4096);
        s.text1b(endpoint_base_dir, 4096);
    }
};

template <>
struct std::hash<HostRequest> {
    std::size_t operator()(HostRequest const& params) const noexcept {
        std::hash<string> hasher{};

        return hasher(params.plugin_path) ^
               (hasher(params.endpoint_base_dir) << 1);
    }
};

/**
 * The response sent back after the group host process receives a `HostRequest`
 * object. This only holds the group process's PID because we need to know if
 * the group process crashes while it is initializing the plugin to prevent us
 * from waiting indefinitely for the socket to be connected to.
 */
struct HostResponse {
    pid_t pid;

    template <typename S>
    void serialize(S& s) {
        s.value4b(pid);
    }
};
