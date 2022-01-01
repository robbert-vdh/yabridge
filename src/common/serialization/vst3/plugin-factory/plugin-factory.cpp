// yabridge: a Wine VST bridge
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

#include "plugin-factory.h"

#include <iostream>
#include <type_traits>

#include <public.sdk/source/vst/utility/stringconvert.h>

YaPluginFactory3::ConstructArgs::ConstructArgs() noexcept {}

YaPluginFactory3::ConstructArgs::ConstructArgs(
    Steinberg::IPtr<Steinberg::FUnknown> object) {
    Steinberg::FUnknownPtr<Steinberg::IPluginFactory> factory(object);
    if (!factory) {
        return;
    }

    supports_plugin_factory = true;
    // `IPluginFactory::getFactoryInfo`
    if (Steinberg::PFactoryInfo info;
        factory->getFactoryInfo(&info) == Steinberg::kResultOk) {
        factory_info = info;
    }
    // `IPluginFactory::countClasses`
    num_classes = factory->countClasses();
    // `IPluginFactory::getClassInfo`
    class_infos_1.resize(num_classes);
    for (int i = 0; i < num_classes; i++) {
        Steinberg::PClassInfo info;
        if (factory->getClassInfo(i, &info) == Steinberg::kResultOk) {
            class_infos_1[i] = info;

            // NOTE: We'll need to do a byte order conversion to the reported
            //       class IDs match up with native and 'real' Windows VST3
            //       plugins. See `WineUID` for more information.
            ArrayUID native_uid = WineUID(info.cid).get_native_uid();
            std::copy(native_uid.begin(), native_uid.end(),
                      class_infos_1[i]->cid);
        }
    }

    Steinberg::FUnknownPtr<Steinberg::IPluginFactory2> factory2(factory);
    if (!factory2) {
        return;
    }

    supports_plugin_factory_2 = true;
    // `IpluginFactory2::getClassInfo2`
    class_infos_2.resize(num_classes);
    for (int i = 0; i < num_classes; i++) {
        Steinberg::PClassInfo2 info;
        if (factory2->getClassInfo2(i, &info) == Steinberg::kResultOk) {
            class_infos_2[i] = info;

            // NOTE: We'll need to do a byte order conversion to the reported
            //       class IDs match up with native and 'real' Windows VST3
            //       plugins. See `WineUID` for more information.
            ArrayUID native_uid = WineUID(info.cid).get_native_uid();
            std::copy(native_uid.begin(), native_uid.end(),
                      class_infos_2[i]->cid);
        }
    }

    Steinberg::FUnknownPtr<Steinberg::IPluginFactory3> factory3(factory);
    if (!factory3) {
        return;
    }

    supports_plugin_factory_3 = true;
    // `IpluginFactory3::getClassInfoUnicode`
    class_infos_unicode.resize(num_classes);
    for (int i = 0; i < num_classes; i++) {
        Steinberg::PClassInfoW info;
        if (factory3->getClassInfoUnicode(i, &info) == Steinberg::kResultOk) {
            class_infos_unicode[i] = info;

            // NOTE: We'll need to do a byte order conversion to the reported
            //       class IDs match up with native and 'real' Windows VST3
            //       plugins. See `WineUID` for more information.
            ArrayUID native_uid = WineUID(info.cid).get_native_uid();
            std::copy(native_uid.begin(), native_uid.end(),
                      class_infos_unicode[i]->cid);
        }
    }
}

YaPluginFactory3::YaPluginFactory3(ConstructArgs&& args) noexcept
    : arguments(std::move(args)) {}

tresult PLUGIN_API
YaPluginFactory3::getFactoryInfo(Steinberg::PFactoryInfo* info) {
    if (info && arguments.factory_info) {
        *info = *arguments.factory_info;
        return Steinberg::kResultOk;
    } else {
        return Steinberg::kNotInitialized;
    }
}

int32 PLUGIN_API YaPluginFactory3::countClasses() {
    return arguments.num_classes;
}

tresult PLUGIN_API YaPluginFactory3::getClassInfo(Steinberg::int32 index,
                                                  Steinberg::PClassInfo* info) {
    if (index >= static_cast<int32>(arguments.class_infos_1.size())) {
        return Steinberg::kInvalidArgument;
    }

    // We will have already converted these class IDs to the native
    // representation in `YaPluginFactory3::ConstructArgs`
    if (arguments.class_infos_1[index]) {
        *info = *arguments.class_infos_1[index];
        return Steinberg::kResultOk;
    } else {
        return Steinberg::kResultFalse;
    }
}

tresult PLUGIN_API
YaPluginFactory3::getClassInfo2(int32 index, Steinberg::PClassInfo2* info) {
    if (index >= static_cast<int32>(arguments.class_infos_2.size())) {
        return Steinberg::kInvalidArgument;
    }

    // We will have already converted these class IDs to the native
    // representation in `YaPluginFactory3::ConstructArgs`
    if (arguments.class_infos_2[index]) {
        *info = *arguments.class_infos_2[index];
        return Steinberg::kResultOk;
    } else {
        return Steinberg::kResultFalse;
    }
}

tresult PLUGIN_API
YaPluginFactory3::getClassInfoUnicode(int32 index,
                                      Steinberg::PClassInfoW* info) {
    if (index >= static_cast<int32>(arguments.class_infos_unicode.size())) {
        return Steinberg::kInvalidArgument;
    }

    // We will have already converted these class IDs to the native
    // representation in `YaPluginFactory3::ConstructArgs`
    if (arguments.class_infos_unicode[index]) {
        *info = *arguments.class_infos_unicode[index];
        return Steinberg::kResultOk;
    } else {
        return Steinberg::kResultFalse;
    }
}
