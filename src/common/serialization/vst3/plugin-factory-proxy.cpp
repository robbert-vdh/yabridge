// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2023 Robbert van der Helm
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

#include "plugin-factory-proxy.h"

Vst3PluginFactoryProxy::ConstructArgs::ConstructArgs() noexcept {}

Vst3PluginFactoryProxy::ConstructArgs::ConstructArgs(
    Steinberg::IPtr<Steinberg::FUnknown> object) noexcept
    : plugin_factory_args(object) {}

Vst3PluginFactoryProxy::Vst3PluginFactoryProxy(ConstructArgs&& args) noexcept
    : YaPluginFactory3(std::move(args.plugin_factory_args)),
      arguments_(std::move(args)){FUNKNOWN_CTOR}

      // clang-format just doesn't understand these macros, I guess
      Vst3PluginFactoryProxy::~Vst3PluginFactoryProxy() noexcept {FUNKNOWN_DTOR}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
      IMPLEMENT_REFCOUNT(Vst3PluginFactoryProxy)
#pragma GCC diagnostic pop

          tresult PLUGIN_API Vst3PluginFactoryProxy::queryInterface(
              Steinberg::FIDString _iid,
              void** obj) {
    if (YaPluginFactory3::supports_plugin_factory()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::FUnknown::iid,
                        Steinberg::IPluginFactory)
        QUERY_INTERFACE(_iid, obj, Steinberg::IPluginFactory::iid,
                        Steinberg::IPluginFactory)
    }
    if (YaPluginFactory3::supports_plugin_factory_2()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::IPluginFactory2::iid,
                        Steinberg::IPluginFactory2)
    }
    if (YaPluginFactory3::supports_plugin_factory_3()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::IPluginFactory3::iid,
                        Steinberg::IPluginFactory3)
    }

    *obj = nullptr;
    return Steinberg::kNoInterface;
}
