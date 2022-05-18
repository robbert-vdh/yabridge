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

#include <variant>

#include <pluginterfaces/vst/ivstmessage.h>
#include "../../../bitsery/ext/in-place-variant.h"

#include "../../../bitsery/ext/in-place-optional.h"
#include "../../common.h"
#include "../base.h"
#include "../message.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IConnectionPoint` for serialization purposes. This is
 * instantiated as part of `Vst3PluginProxy`. Because we use this giant
 * monolithic proxy class we can easily directly connect different objects by
 * checking if they're a `Vst3PluginProxy` and then fetching that object's
 * instance ID (if the host doesn't place a proxy object here).
 */
class YaConnectionPoint : public Steinberg::Vst::IConnectionPoint {
   public:
    /**
     * These are the arguments for creating a `YaConnectionPoint`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements
         * `IConnectionPoint` and read arguments from it.
         */
        ConstructArgs(Steinberg::IPtr<Steinberg::FUnknown> object) noexcept;

        /**
         * Whether the object supported this interface.
         */
        bool supported;

        template <typename S>
        void serialize(S& s) {
            s.value1b(supported);
        }
    };

   protected:
    /**
     * These are the arguments for constructing a
     * `Vst3ConnectionPointProxyImpl`.
     *
     * It's defined here to work around circular includes.
     */
    struct Vst3ConnectionPointProxyConstructArgs {
        Vst3ConnectionPointProxyConstructArgs() noexcept;

        /**
         * Read from an existing object. We will try to mimic this object, so
         * we'll support any interfaces this object also supports.
         *
         * This is not necessary in this case since the object has to support
         * `IConnectionPoint`, but let's stay consistent with the overall style
         * here.
         */
        Vst3ConnectionPointProxyConstructArgs(
            Steinberg::IPtr<FUnknown> object,
            size_t owner_instance_id) noexcept;

        /**
         * The unique instance identifier of the proxy object instance this
         * connection proxy has been passed to and thus belongs to. This way we
         * can refer to the correct 'actual' `IConnectionPoint` instance when
         * the plugin calls `notify()` on this proxy object.
         */
        native_size_t owner_instance_id;

        ConstructArgs connection_point_args;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.object(connection_point_args);
        }
    };

   public:
    /**
     * Instantiate this instance with arguments read from another interface
     * implementation.
     */
    YaConnectionPoint(ConstructArgs&& args) noexcept;

    virtual ~YaConnectionPoint() noexcept = default;

    inline bool supported() const noexcept { return arguments_.supported; }

    /**
     * Message to pass through a call to `IConnectionPoint::connect(other)` to
     * the Wine plugin host. If the host directly connects two objects, then
     * we'll connect them directly as well. Otherwise all messages have to be
     * routed through the host.
     */
    struct Connect {
        using Response = UniversalTResult;

        native_size_t instance_id;

        /**
         * The other object this object should be connected to. When connecting
         * two `Vst3PluginProxy` objects, we can directly connect the underlying
         * objects on the Wine side using their instance IDs. Otherwise we'll
         * create a proxy object for the connection proxy provided by the host
         * that the plugin can use to send messages to.
         */
        std::variant<native_size_t, Vst3ConnectionPointProxyConstructArgs>
            other;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.ext(other,
                  bitsery::ext::InPlaceVariant{
                      [](S& s, native_size_t& other_instance_id) {
                          s.value8b(other_instance_id);
                      },
                      [](S& s, Vst3ConnectionPointProxyConstructArgs& args) {
                          s.object(args);
                      }});
        }
    };

    virtual tresult PLUGIN_API connect(IConnectionPoint* other) override = 0;

    /**
     * Message to pass through a call to `IConnectionPoint::disconnect(other)`
     * to the Wine plugin host.
     */
    struct Disconnect {
        using Response = UniversalTResult;

        native_size_t instance_id;

        /**
         * If we connected two objects directly, then this is the instance ID of
         * that object. Otherwise we'll just destroy the smart pointer pointing
         * to our `IConnectionPoint` proxy object.
         */
        std::optional<native_size_t> other_instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.ext(other_instance_id, bitsery::ext::InPlaceOptional{},
                  [](S& s, native_size_t& instance_id) {
                      s.value8b(instance_id);
                  });
        }
    };

    virtual tresult PLUGIN_API disconnect(IConnectionPoint* other) override = 0;

    /**
     * Message to pass through a call to `IConnectionPoint::notify(message)` to
     * the Wine plugin host. Since `IAttributeList` does not have any way to
     * iterate over all values, we only support messages sent by plugins using
     * our own implementation of the interface, since there's no way to
     * serialize them otherwise. Additionally, plugins may store the `IMessage`
     * pointer for later usage, so we have to pass through a pointer to the
     * original message so it has the same lifetime as the original message.
     * This `IConnectionPoint::notify()` implementation is also only used with
     * hosts that do not connect objects directly and use connection proxies
     * instead.
     */
    struct Notify {
        using Response = UniversalTResult;

        native_size_t instance_id;

        YaMessagePtr message_ptr;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.object(message_ptr);
        }
    };

    virtual tresult PLUGIN_API
    notify(Steinberg::Vst::IMessage* message) override = 0;

   protected:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop
