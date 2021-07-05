// yabridge: a Wine VST bridge
// Copyright (C) 2020-2021 Robbert van der Helm
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

#include "../common.h"
#include "plugin/connection-point.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * This is only needed to...proxy a connection point proxy. Most hosts will
 * connect a plugin's processor and controller directly using
 * `IConnectionPoint::connect()`. But some hosts, like Ardour, will place a
 * proxy object between them that forwards calls to
 * `IConnectionPoint::notify()`. When objects are connected directly by the host
 * we can also connect them directly in the Wine plugin host, but when the host
 * uses proxies we'll also have to go through that proxy. The purpose of this
 * class is to provide a proxy for such a connection proxy. So when the plugin
 * calls `notify()` on an object of this class, then we will forward that call
 * to the `IConnectionPoint` proxy provided by the host, which will then in turn
 * call `IConnectionPoint::notify()` on the other object and we'll then forward
 * that message again to them Wine plugin host.
 */
class Vst3ConnectionPointProxy : public YaConnectionPoint {
   public:
    // We had to define this in `YaConnectionPoint` to work around circular
    // includes
    using ConstructArgs =
        YaConnectionPoint::Vst3ConnectionPointProxyConstructArgs;

    /**
     * Instantiate this instance with arguments read from an actual
     * `IConnectionPoint` object/proxy.
     *
     * @note This object will be created as part of handling
     *   `IConnectionPoint::connect()` if the connection is indirect.
     */
    Vst3ConnectionPointProxy(ConstructArgs&& args) noexcept;

    /**
     * This object will be destroyed again during
     * `IConnectionPoint::disconnect()`.
     */
    virtual ~Vst3ConnectionPointProxy() noexcept;

    DECLARE_FUNKNOWN_METHODS

    /**
     * Get the instance ID of the owner of this object.
     */
    inline size_t owner_instance_id() const noexcept {
        return arguments.owner_instance_id;
    }

   private:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop
