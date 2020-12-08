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

#include "vst3.h"

// These are implementation of the serialization clases in
// `src/common/serialization/vst3/` to provide callback support
// TODO: Split this up in multiple headers. I hoped it might stay small and easy
//       to oversee. It won't.

class YaPluginFactoryPluginImpl : public YaPluginFactory {
   public:
    YaPluginFactoryPluginImpl(Vst3PluginBridge& bridge);

    tresult PLUGIN_API createInstance(Steinberg::FIDString cid,
                                      Steinberg::FIDString _iid,
                                      void** obj) override;
    tresult PLUGIN_API setHostContext(Steinberg::FUnknown* context) override;

   private:
    Vst3PluginBridge& bridge;
};

class YaComponentPluginImpl : public YaComponent {
   public:
    YaComponentPluginImpl(Vst3PluginBridge& bridge,
                          YaComponent::Arguments&& args);

    /**
     * When the reference count reaches zero and this destructor is called,
     * we'll send a request to the Wine plugin host to destroy the corresponding
     * object.
     */
    ~YaComponentPluginImpl();

    /**
     * We'll override the query interface to log queries for interfaces we do
     * not (yet) support.
     */
    tresult PLUGIN_API queryInterface(const ::Steinberg::TUID _iid,
                                      void** obj) override;

    tresult PLUGIN_API initialize(FUnknown* context) override;
    tresult PLUGIN_API terminate() override;

    tresult PLUGIN_API setIoMode(Steinberg::Vst::IoMode mode) override;
    int32 PLUGIN_API getBusCount(Steinberg::Vst::MediaType type,
                                 Steinberg::Vst::BusDirection dir) override;
    tresult PLUGIN_API
    getBusInfo(Steinberg::Vst::MediaType type,
               Steinberg::Vst::BusDirection dir,
               int32 index,
               Steinberg::Vst::BusInfo& bus /*out*/) override;
    tresult PLUGIN_API
    getRoutingInfo(Steinberg::Vst::RoutingInfo& inInfo,
                   Steinberg::Vst::RoutingInfo& outInfo /*out*/) override;
    tresult PLUGIN_API activateBus(Steinberg::Vst::MediaType type,
                                   Steinberg::Vst::BusDirection dir,
                                   int32 index,
                                   TBool state) override;
    tresult PLUGIN_API setActive(TBool state) override;
    tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

   private:
    Vst3PluginBridge& bridge;
};
