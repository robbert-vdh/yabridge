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

#include "../vst3.h"

class YaPluginFactoryImpl : public YaPluginFactory {
   public:
    YaPluginFactoryImpl(Vst3PluginBridge& bridge,
                        YaPluginFactory::ConstructArgs&& args);

    tresult PLUGIN_API createInstance(Steinberg::FIDString cid,
                                      Steinberg::FIDString _iid,
                                      void** obj) override;
    tresult PLUGIN_API setHostContext(Steinberg::FUnknown* context) override;

    // The following pointers are cast from `host_context` if
    // `IPluginFactory3::setHostContext()` has been called

    Steinberg::FUnknownPtr<Steinberg::Vst::IHostApplication> host_application;

   private:
    Vst3PluginBridge& bridge;

    /**
     * An host context if we get passed one through
     * `IPluginFactory3::setHostContext()`.
     */
    Steinberg::IPtr<Steinberg::FUnknown> host_context;
};
