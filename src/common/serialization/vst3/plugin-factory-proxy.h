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

#include "../common.h"
#include "plugin-factory/plugin-factory.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * An abstract class that `IPluginFactory`, and optionally also
 * `IPluginFactory2` and `IPluginFactory3` depending on what the Windows VST3
 * plugin's plugin factory supports. All information is read once the Wine
 * plugin host side, so the only callbacks that we'll make from here are to
 * create new objects and to set a host context for the factory (if the host and
 * the plugin supports that).
 */
class Vst3PluginFactoryProxy : public YaPluginFactory3 {
   public:
    /**
     * These are the arguments for constructing a `Vst3PluginFactoryProxyImpl`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Read from an existing object. We will try to mimic this object, so
         * we'll support any interfaces this object also supports.
         */
        ConstructArgs(Steinberg::IPtr<FUnknown> object) noexcept;

        YaPluginFactory3::ConstructArgs plugin_factory_args;

        template <typename S>
        void serialize(S& s) {
            s.object(plugin_factory_args);
        }
    };

    /**
     * Message to request the Windows VST3 plugin's plugin factory information
     * from the Wine plugin host.
     */
    struct Construct {
        using Response = ConstructArgs;

        template <typename S>
        void serialize(S&) {}
    };

    /**
     * Instantiate this instance with arguments read from an actual plugin
     * factory. The is done once during startup and the plugin factory gets
     * reused for the lifetime of the module.
     */
    Vst3PluginFactoryProxy(ConstructArgs&& args) noexcept;

    /**
     * We do not need special handling here since the Window VST3 plugin's
     * plugin factory will also be destroyed when we terminate the Wine plugin
     * host or unload the plugin there.
     */
    virtual ~Vst3PluginFactoryProxy() noexcept;

    DECLARE_FUNKNOWN_METHODS

   private:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop
