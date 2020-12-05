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

#include <pluginterfaces/base/ipluginbase.h>

#include "../../bitsery/ext/vst3.h"

namespace {
using Steinberg::int32, Steinberg::tresult;
}  // namespace

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IPluginFactory{1,2,3}` for serialization purposes. See
 * `README.md` for more information on how this works.
 */
class YaPluginFactory : public Steinberg::IPluginFactory3 {
   public:
    YaPluginFactory();

    /**
     * Create a copy of an existing plugin factory. Depending on the
     supported
     * interface function more or less of this struct will be left empty, and
     * `iid` will be set accordingly.
     *
     * TODO: Check if we don't need a custom query interface, we probably do.
     */
    explicit YaPluginFactory(Steinberg::IPluginFactory* factory);

    ~YaPluginFactory();

    DECLARE_FUNKNOWN_METHODS

    // From `IPluginFactory`
    tresult PLUGIN_API getFactoryInfo(Steinberg::PFactoryInfo* info) override;
    int32 PLUGIN_API countClasses() override;
    tresult PLUGIN_API getClassInfo(Steinberg::int32 index,
                                    Steinberg::PClassInfo* info) override;
    tresult PLUGIN_API createInstance(Steinberg::FIDString cid,
                                      Steinberg::FIDString _iid,
                                      void** obj) override;

    // From `IPluginFactory2`
    tresult PLUGIN_API getClassInfo2(int32 index,
                                     Steinberg::PClassInfo2* info) override;

    // From `IPluginFactory3`
    tresult PLUGIN_API
    getClassInfoUnicode(int32 index, Steinberg::PClassInfoW* info) override;
    tresult PLUGIN_API setHostContext(Steinberg::FUnknown* context) override;

    /**
     * The IID of the interface we should report as.
     */
    Steinberg::FUID actual_iid;

    template <typename S>
    void serialize(S& s) {
        s.ext(actual_iid, bitsery::ext::FUID());
    }
};

#pragma GCC diagnostic pop
