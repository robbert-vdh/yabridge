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

#include "plugin-factory.h"

#include <iostream>
#include <type_traits>

#include <pluginterfaces/base/ftypes.h>
#include <pluginterfaces/base/funknown.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <public.sdk/source/vst/utility/stringconvert.h>

/**
 * Return whether yabridge supports this class or not. This way we can skip over
 * any classes that the plugin might support but we have not implemented yet. If
 * we do not support a class, we will log it.
 *
 * @tparam Any of `Steinberg::PClassInfo`, `Steinberg::PClassInfo2` or
 *   `Steinberg::PClassInfoW`.
 */
template <typename T>
bool is_supported_interface(const T& class_info);

YaPluginFactory::YaPluginFactory(){FUNKNOWN_CTOR}

YaPluginFactory::YaPluginFactory(
    Steinberg::IPtr<Steinberg::IPluginFactory> factory) {
    FUNKNOWN_CTOR

    known_iids.insert(factory->iid);
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
        if (factory->getClassInfo(i, &info) == Steinberg::kResultOk &&
            is_supported_interface(info)) {
            class_infos_1[i] = info;
        }
    }

    auto factory2 = Steinberg::FUnknownPtr<Steinberg::IPluginFactory2>(factory);
    if (!factory2) {
        return;
    }

    known_iids.insert(factory2->iid);
    // `IpluginFactory2::getClassInfo2`
    for (int i = 0; i < num_classes; i++) {
        Steinberg::PClassInfo2 info;
        if (factory2->getClassInfo2(i, &info) == Steinberg::kResultOk &&
            is_supported_interface(info)) {
            class_infos_2[i] = info;
        }
    }

    auto factory3 = Steinberg::FUnknownPtr<Steinberg::IPluginFactory3>(factory);
    if (!factory3) {
        return;
    }

    known_iids.insert(factory3->iid);
    // `IpluginFactory3::getClassInfoUnicode`
    for (int i = 0; i < num_classes; i++) {
        Steinberg::PClassInfoW info;
        if (factory3->getClassInfoUnicode(i, &info) == Steinberg::kResultOk &&
            is_supported_interface(info)) {
            class_infos_unicode[i] = info;
        }
    }
}

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
    if (known_iids.contains(Steinberg::IPluginFactory::iid)) {
        QUERY_INTERFACE(_iid, obj, Steinberg::IPluginFactory::iid,
                        Steinberg::IPluginFactory)
    }
    if (known_iids.contains(Steinberg::IPluginFactory2::iid)) {
        QUERY_INTERFACE(_iid, obj, Steinberg::IPluginFactory2::iid,
                        Steinberg::IPluginFactory2)
    }
    if (known_iids.contains(Steinberg::IPluginFactory3::iid)) {
        QUERY_INTERFACE(_iid, obj, Steinberg::IPluginFactory3::iid,
                        Steinberg::IPluginFactory3)
    }

    *obj = nullptr;
    return Steinberg::kNoInterface;
}

tresult PLUGIN_API
YaPluginFactory::getFactoryInfo(Steinberg::PFactoryInfo* info) {
    if (info && factory_info) {
        *info = *factory_info;
        return Steinberg::kResultOk;
    } else {
        return Steinberg::kNotInitialized;
    }
}

int32 PLUGIN_API YaPluginFactory::countClasses() {
    return num_classes;
}

tresult PLUGIN_API YaPluginFactory::getClassInfo(Steinberg::int32 index,
                                                 Steinberg::PClassInfo* info) {
    if (index >= static_cast<int32>(class_infos_unicode.size())) {
        return Steinberg::kInvalidArgument;
    }

    if (class_infos_1[index]) {
        *info = *class_infos_1[index];
        return Steinberg::kResultOk;
    } else {
        return Steinberg::kResultFalse;
    }
}

tresult PLUGIN_API
YaPluginFactory::getClassInfo2(int32 index, Steinberg::PClassInfo2* info) {
    if (index >= static_cast<int32>(class_infos_1.size())) {
        return Steinberg::kInvalidArgument;
    }

    if (class_infos_2[index]) {
        *info = *class_infos_2[index];
        return Steinberg::kResultOk;
    } else {
        return Steinberg::kResultFalse;
    }
}

tresult PLUGIN_API
YaPluginFactory::getClassInfoUnicode(int32 index,
                                     Steinberg::PClassInfoW* info) {
    if (index >= static_cast<int32>(class_infos_unicode.size())) {
        return Steinberg::kInvalidArgument;
    }

    if (class_infos_unicode[index]) {
        *info = *class_infos_unicode[index];
        return Steinberg::kResultOk;
    } else {
        return Steinberg::kResultFalse;
    }
}

template <typename T>
bool is_supported_interface(const T& class_info) {
    // I feel like we're not supposed to use this comparison function, but they
    // don't offer any other ways to compare FUIDs/TUIDs
    // TODO: Add these interfaces as we go along
    if (Steinberg::FUnknownPrivate::iidEqual(class_info.cid,
                                             Steinberg::Vst::IComponent::iid)
        // ||
        // Steinberg::FUnknownPrivate::iidEqual(
        //     cid, Steinberg::Vst::IAudioProcessor::iid) ||
        // Steinberg::FUnknownPrivate::iidEqual(
        //     cid, Steinberg::Vst::IEditController::iid)
    ) {
        return true;
    } else {
        // TODO: These prints get logged correctly because we do this from the
        //       Wine side, but for neater logging we should add these to a list
        //       instead and then print them all when we receive the factory
        //       instance on the plugin's side
        std::string class_name = VST3::StringConvert::convert(
            class_info.name, Steinberg::PClassInfo::kNameSize);

        char interface_id_str[128];
        Steinberg::FUID(class_info.cid)
            .print(interface_id_str, Steinberg::FUID::UIDPrintStyle::kFUID);

        std::cerr << "Unsupported interface '" << class_name
                  << "': " << interface_id_str << std::endl;

        return false;
    }
}
