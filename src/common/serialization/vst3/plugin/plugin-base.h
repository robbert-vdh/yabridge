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

#include <pluginterfaces/base/ipluginbase.h>

#include "../../common.h"
#include "../base.h"
#include "../host-context-proxy.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IPluginBase` for serialization purposes. Both components and
 * edit controllers inherit from this. This is instantiated as part of
 * `Vst3PluginProxy`.
 */
class YaPluginBase : public Steinberg::IPluginBase {
   public:
    /**
     * These are the arguments for creating a `YaPluginBase`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements `IPluginBase` and
         * read arguments from it.
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

    /**
     * Instantiate this instance with arguments read from another interface
     * implementation.
     */
    YaPluginBase(const ConstructArgs&& args) noexcept;

    inline bool supported() const noexcept { return arguments.supported; }

    /**
     * Message to pass through a call to `IPluginBase::initialize()` to the Wine
     * plugin host. We will read what interfaces the passed context object
     * implements so we can then create a proxy object on the Wine side that the
     * plugin can use to make callbacks with. The lifetime of this
     * `Vst3HostContextProxy` object should be bound to the `IComponent` we are
     * proxying.
     */
    struct Initialize {
        using Response = UniversalTResult;

        native_size_t instance_id;

        Vst3HostContextProxy::ConstructArgs host_context_args;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.object(host_context_args);
        }
    };

    virtual tresult PLUGIN_API initialize(FUnknown* context) override = 0;

    /**
     * Message to pass through a call to `IPluginBase::terminate()` to the Wine
     * plugin host.
     */
    struct Terminate {
        using Response = UniversalTResult;

        native_size_t instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
        }
    };

    virtual tresult PLUGIN_API terminate() override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop
