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
#include <pluginterfaces/base/ipluginbase.h>

#include "../common.h"
#include "base.h"
#include "host-application.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IPluginBase` for serialization purposes. Both components and
 * edit controllers inherit from this. This is instantiated as part of
 * `YaPluginMonolith`.
 */
class YaPluginBase : public Steinberg::IPluginBase {
   public:
    /**
     * These are the arguments for creating a `YaPluginBase`.
     */
    struct ConstructArgs {
        ConstructArgs();

        /**
         * Check whether an existing implementation implements `IPluginBase` and
         * read arguments from it.
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
    YaPluginBase(const ConstructArgs&& args);

    inline bool supported() { return arguments.supported; }

    /**
     * Message to pass through a call to `IPluginBase::initialize()` to the Wine
     * plugin host. if we pass an `IHostApplication` instance, then a proxy
     * `YaHostApplication` should be created and passed as an argument to
     * `IPluginBase::initialize()`. If this is absent a null pointer should be
     * passed. The lifetime of this `YaHostApplication` object should be bound
     * to the `IComponent` we are proxying.
     */
    struct Initialize {
        using Response = UniversalTResult;

        native_size_t instance_id;
        std::optional<YaHostApplication::ConstructArgs>
            host_application_context_args;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.ext(host_application_context_args, bitsery::ext::StdOptional{});
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
