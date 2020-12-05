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

YaPluginFactory::YaPluginFactory(){FUNKNOWN_CTOR}

YaPluginFactory::YaPluginFactory(
    Steinberg::IPtr<Steinberg::IPluginFactory> factory) {
    FUNKNOWN_CTOR

    // TODO: Copy data from `IPluginFactory`
    // TODO: We should only copy the interfaces that we support. This should use
    //       the same list as that used in `createInstance()`.
    known_iids.insert(factory->iid);

    auto factory2 = Steinberg::FUnknownPtr<Steinberg::IPluginFactory2>(factory);
    if (!factory2) {
        return;
    }

    // TODO: Copy data from `IPluginFactory2`
    known_iids.insert(factory2->iid);

    auto factory3 = Steinberg::FUnknownPtr<Steinberg::IPluginFactory3>(factory);
    if (!factory3) {
        return;
    }

    // TODO: Copy data from `IPluginFactory3`
    known_iids.insert(factory3->iid);
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
YaPluginFactory::getFactoryInfo(Steinberg::PFactoryInfo* /*info*/) {
    // TODO: Implement
    return 0;
}

int32 PLUGIN_API YaPluginFactory::countClasses() {
    // TODO: Implement
    return 0;
}

tresult PLUGIN_API
YaPluginFactory::getClassInfo(Steinberg::int32 /*index*/,
                              Steinberg::PClassInfo* /*info*/) {
    // TODO: Implement
    return 0;
}

tresult PLUGIN_API
YaPluginFactory::createInstance(Steinberg::FIDString /*cid*/,
                                Steinberg::FIDString /*_iid*/,
                                void** /*obj*/) {
    // TODO: Figure out how to implement this. Some considerations:
    //       - We have to sent a control message to the Wine plugin host to ask
    //         it to create an instance of `_iid`.
    //       - We then create a `Ya*` implementation of the same interface on
    //         the plugin side.
    //       - These two should be wired up so that when the host calls a
    //         function on it, it should be sent to the instance on the Wine
    //         plugin host side with the same cid.
    //       - We should have a list of interfaces we support. When we receive a
    //         request to create an instance of something we don't support, then
    //         we should log that and then fail.
    return 0;
}

tresult PLUGIN_API
YaPluginFactory::getClassInfo2(int32 /*index*/,
                               Steinberg::PClassInfo2* /*info*/) {
    // TODO: Implement
    return 0;
}

tresult PLUGIN_API
YaPluginFactory::getClassInfoUnicode(int32 /*index*/,
                                     Steinberg::PClassInfoW* /*info*/) {
    // TODO: Implement
    return 0;
}

tresult PLUGIN_API
YaPluginFactory::setHostContext(Steinberg::FUnknown* /*context*/) {
    // TODO: Implement
    return 0;
}
