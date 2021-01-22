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

#include "plugin-factory.h"

#include <iostream>
#include <type_traits>

#include <public.sdk/source/vst/utility/stringconvert.h>

YaPluginFactory::ConstructArgs::ConstructArgs() {}

YaPluginFactory::ConstructArgs::ConstructArgs(
    Steinberg::IPtr<Steinberg::IPluginFactory> factory) {
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
                      class_infos_1[i]->cid);
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
                      class_infos_1[i]->cid);
        }
    }
}

YaPluginFactory::YaPluginFactory(const ConstructArgs&& args)
    : arguments(std::move(args)){FUNKNOWN_CTOR}

      // clang-format just doesn't understand these macros, I guess
      YaPluginFactory::~YaPluginFactory() {
    FUNKNOWN_DTOR
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
IMPLEMENT_REFCOUNT(YaPluginFactory)
#pragma GCC diagnostic pop

tresult PLUGIN_API YaPluginFactory::queryInterface(Steinberg::FIDString _iid,
                                                   void** obj) {
    QUERY_INTERFACE(_iid, obj, Steinberg::FUnknown::iid,
                    Steinberg::IPluginFactory)
    QUERY_INTERFACE(_iid, obj, Steinberg::IPluginFactory::iid,
                    Steinberg::IPluginFactory)
    if (arguments.supports_plugin_factory_2) {
        QUERY_INTERFACE(_iid, obj, Steinberg::IPluginFactory2::iid,
                        Steinberg::IPluginFactory2)
    }
    if (arguments.supports_plugin_factory_3) {
        QUERY_INTERFACE(_iid, obj, Steinberg::IPluginFactory3::iid,
                        Steinberg::IPluginFactory3)
    }

    *obj = nullptr;
    return Steinberg::kNoInterface;
}

tresult PLUGIN_API
YaPluginFactory::getFactoryInfo(Steinberg::PFactoryInfo* info) {
    if (info && arguments.factory_info) {
        *info = *arguments.factory_info;
        return Steinberg::kResultOk;
    } else {
        return Steinberg::kNotInitialized;
    }
}

int32 PLUGIN_API YaPluginFactory::countClasses() {
    return arguments.num_classes;
}

tresult PLUGIN_API YaPluginFactory::getClassInfo(Steinberg::int32 index,
                                                 Steinberg::PClassInfo* info) {
    if (index >= static_cast<int32>(arguments.class_infos_1.size())) {
        return Steinberg::kInvalidArgument;
    }

    // FIXME: The class IDs are incorrect! See the `INLINE_UID` macro. We need
    //        to shuffle the byte orders around for plugins to be compatible
    //        with projects saved under Windows and with native Linux versions
    //        of the same plugin. We need to do this transformation for all of
    //        these functions
    // FIXME: We need to do similar translations everywhere where we encounter
    //        `ArrayUID`, such as `IComponent::getControllerClassId()`
    if (arguments.class_infos_1[index]) {
        *info = *arguments.class_infos_1[index];
        return Steinberg::kResultOk;
    } else {
        return Steinberg::kResultFalse;
    }
}

tresult PLUGIN_API
YaPluginFactory::getClassInfo2(int32 index, Steinberg::PClassInfo2* info) {
    if (index >= static_cast<int32>(arguments.class_infos_2.size())) {
        return Steinberg::kInvalidArgument;
    }

    if (arguments.class_infos_2[index]) {
        *info = *arguments.class_infos_2[index];
        return Steinberg::kResultOk;
    } else {
        return Steinberg::kResultFalse;
    }
}

tresult PLUGIN_API
YaPluginFactory::getClassInfoUnicode(int32 index,
                                     Steinberg::PClassInfoW* info) {
    if (index >= static_cast<int32>(arguments.class_infos_unicode.size())) {
        return Steinberg::kInvalidArgument;
    }

    if (arguments.class_infos_unicode[index]) {
        *info = *arguments.class_infos_unicode[index];
        return Steinberg::kResultOk;
    } else {
        return Steinberg::kResultFalse;
    }
}
