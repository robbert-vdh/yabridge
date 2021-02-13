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

#include "../../../common/serialization/vst3/plugin-factory-proxy.h"

// We need an `IPtr<Vst3PluginFactoryProxyImpl>` in `Vst3PluginBridge`, so we
// need to declare this slightly differently to avoid circular includes.
class Vst3PluginBridge;

class Vst3PluginFactoryProxyImpl : public Vst3PluginFactoryProxy {
   public:
    Vst3PluginFactoryProxyImpl(Vst3PluginBridge& bridge,
                               Vst3PluginFactoryProxy::ConstructArgs&& args);

    /**
     * We'll override the query interface to log queries for interfaces we do
     * not (yet) support.
     */
    tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                      void** obj) override;

    tresult PLUGIN_API createInstance(Steinberg::FIDString cid,
                                      Steinberg::FIDString _iid,
                                      void** obj) override;
    tresult PLUGIN_API setHostContext(Steinberg::FUnknown* context) override;

    // The following pointers are cast from `host_context` if
    // `IPluginFactory3::setHostContext()` has been called

    Steinberg::FUnknownPtr<Steinberg::Vst::IHostApplication> host_application;
    Steinberg::FUnknownPtr<Steinberg::Vst::IPlugInterfaceSupport>
        plug_interface_support;

   private:
    Vst3PluginBridge& bridge;

    /**
     * An host context if we get passed one through
     * `IPluginFactory3::setHostContext()`.
     */
    Steinberg::IPtr<Steinberg::FUnknown> host_context;
};
