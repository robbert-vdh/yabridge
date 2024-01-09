// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2024 Robbert van der Helm
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

#include "../configuration.h"
#include "../plugins.h"

// The plugin should always be compiled to a 64-bit version, but the host
// application can also be 32-bit to allow using 32-bit legacy Windows VST in a
// modern Linux VST host. Because of this we have to make sure to always use
// 64-bit integers in places where we would otherwise use `size_t` and
// `intptr_t`. Otherwise the binary serialization would break. The 64 <-> 32 bit
// conversion for the 32-bit host application won't cause any issues for us
// since we can't directly pass pointers between the plugin and the host anyway.
using native_size_t = uint64_t;
using native_intptr_t = int64_t;

/**
 * Empty struct for when we have send a response to some operation without any
 * result values.
 */
struct Ack {
    template <typename S>
    void serialize(S&) {}
};

/**
 * A simple wrapper around primitive values for serialization purposes. Bitsery
 * doesn't seem to like serializing plain primitives using `s.object()` even if
 * you define a serialization function.
 */
template <typename T>
class PrimitiveResponse {
   public:
    PrimitiveResponse() noexcept {}
    PrimitiveResponse(T value) noexcept : value_(value) {}

    operator T() const noexcept { return value_; }

    template <typename S>
    void serialize(S& s) {
        s.template value<sizeof(T)>(value_);
    }

   private:
    T value_;
};

/**
 * Marker struct to indicate the other side (the plugin) should send a copy of
 * the configuration. During this process we will also transmit the version
 * string from the host, so we can show a little warning when the user forgot to
 * rerun `yabridgectl sync` (and the initialization was still successful).
 */
struct WantsConfiguration {
    using Response = Configuration;

    std::string host_version;

    template <typename S>
    void serialize(S& s) {
        s.text1b(host_version, 128);
    }
};

/**
 * An object containing the startup options for hosting a plugin. These options
 * are passed to `yabridge-host.exe` as command line arguments, and they are
 * used directly by group host processes.
 */
struct HostRequest {
    PluginType plugin_type;
    std::string plugin_path;
    std::string endpoint_base_dir;
    pid_t parent_pid;

    template <typename S>
    void serialize(S& s) {
        s.object(plugin_type);
        s.text1b(plugin_path, 4096);
        s.text1b(endpoint_base_dir, 4096);
        s.value4b(parent_pid);
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

/**
 * A reference wrapper similar `std::reference_wrapper<T>` that supports default
 * initializing (which is of course UB, but we need this for serialization) and
 * also forwards the `T::Response` type for use with the
 * `TypedMessageHandler`.
 *
 * We use this during audio processing to avoid having to store the actual
 * process data in a temporary object (when we copy it to an
 * `std::variant<Ts...>`) during audio processing. The process data refers to
 * heap data, so copying it would also require performing heap allocations.
 *
 * Since this object only stores a reference to the actual data, serialization
 * must be done using our `bitsery::ext::MessageReference`. On serialization
 * this extension simply reads from the referred object, and on deserialization
 * (when we're actually deserializing into an empty object) we will read into an
 * `std::optional<T>` and then reassign this reference to point to that data, so
 * that the actual backing object can be reused.
 */
template <typename T>
class MessageReference {
   public:
    /**
     * The default constructor is required for our serialization, but it should
     * never be used manually. Calling `.get()` on a default initialized
     * `MessageReference()` results in UB. We'll set the default pointer to
     * `0x1337420` so it's at least obvious where it's coming from if we get a
     * segfault caused by a read to that address.
     */
    MessageReference() noexcept : object_(reinterpret_cast<T*>(0x1337420)) {}

    /**
     * Store a reference in this object.
     */
    MessageReference(T& object) noexcept : object_(&object) {}

    using Response = typename T::Response;

    /**
     * Get the reference to the object. This is the same interface as
     * `std::reference_wrapper<T>`.
     */
    T& get() const noexcept { return *object_; }
    constexpr operator T&() const noexcept { return *object_; }

    // You cannot serialize a reference directly, use the Bitsery extension
    // mentioned above instead

   private:
    T* object_;
};
