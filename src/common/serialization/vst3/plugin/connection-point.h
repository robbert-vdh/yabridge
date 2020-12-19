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

#include <bitsery/ext/std_optional.h>
#include <pluginterfaces/vst/ivstmessage.h>

#include "../../common.h"
#include "../base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IConnectionPoint` for serialization purposes. This is
 * instantiated as part of `Vst3PluginProxy`. Because we use this giant
 * monolithic proxy class we can easily directly connect different objects by
 * checking if they're a `Vst3PluginProxy` and then fetching that object's
 * instance ID (if the host doesn't place a proxy object here).
 *
 * TODO: Make sure we somehow handle proxies created by the host here.
 */
class YaConnectionPoint : public Steinberg::Vst::IConnectionPoint {
   public:
    /**
     * These are the arguments for creating a `YaConnectionPoint`.
     */
    struct ConstructArgs {
        ConstructArgs();

        /**
         * Check whether an existing implementation implements
         * `IConnectionPoint` and read arguments from it.
         */
        ConstructArgs(Steinberg::IPtr<Steinberg::FUnknown> object);

        /**
         * Whether the object supported this interface.
         */
        bool supported;

        template <typename S>
        void serialize(S& s) {
            s.value1b(supported);
        }
    };

    /**
     * Instantiate this instance with arguments read from another interface
     * implementation.
     */
    YaConnectionPoint(const ConstructArgs&& args);

    inline bool supported() const { return arguments.supported; }

    /**
     * Message to pass through a call to
     * `IConnectionPoint::connect(other_instance_id)` to the Wine plugin host.
     * At the moment this is only implemented for directly connecting objects
     * created by the plugin without any proxies in between them.
     */
    struct Connect {
        using Response = UniversalTResult;

        native_size_t instance_id;

        /**
         * The other object this object should be connected to. When connecting
         * two `Vst3PluginProxy` objects, we can directly connect the underlying
         * objects on the Wine side.
         */
        native_size_t other_instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value8b(other_instance_id);
        }
    };

    virtual tresult PLUGIN_API connect(IConnectionPoint* other) override = 0;

    /**
     * Message to pass through a call to
     * `IConnectionPoint::disconnect(other_instance_id)` to the Wine plugin
     * host. At the moment this is only implemented for directly connecting
     * objects created by the plugin without any proxies in between them.
     */
    struct Disconnect {
        using Response = UniversalTResult;

        native_size_t instance_id;

        /**
         * The other object backed by a `Vst3PluginProxy` this object was
         * connected to and should be disconnected from. When connecting.
         */
        native_size_t other_instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value8b(other_instance_id);
        }
    };

    virtual tresult PLUGIN_API disconnect(IConnectionPoint* other) override = 0;
    virtual tresult PLUGIN_API
    notify(Steinberg::Vst::IMessage* message) override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop
