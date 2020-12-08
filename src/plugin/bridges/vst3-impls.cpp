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

#include "vst3-impls.h"

#include <pluginterfaces/vst/ivstcomponent.h>

YaPluginFactoryPluginImpl::YaPluginFactoryPluginImpl(Vst3PluginBridge& bridge)
    : bridge(bridge) {}

tresult PLUGIN_API
YaPluginFactoryPluginImpl::createInstance(Steinberg::FIDString cid,
                                          Steinberg::FIDString _iid,
                                          void** obj) {
    // TODO: Implement as specified in `src/common/serialization/vst3/README.md`
    if (Steinberg::FIDStringsEqual(_iid, Steinberg::Vst::IComponent::iid)) {
        // TODO: Instantiate an IComponent as described above
        return Steinberg::kNotImplemented;
    } else {
        // When the host requests an interface we do not (yet) implement, we'll
        // print a recognizable log message. I don't think they include a safe
        // way to convert a `FIDString/char*` into a `FUID`, so this will have
        // to do.
        char iid_string[128] = "<invalid_pointer>";
        constexpr size_t uid_size = sizeof(Steinberg::TUID);
        if (_iid && strnlen(_iid, uid_size + 1) == uid_size) {
            Steinberg::FUID iid = Steinberg::FUID::fromTUID(
                *reinterpret_cast<const Steinberg::TUID*>(&_iid));
            iid.print(iid_string, Steinberg::FUID::UIDPrintStyle::kCLASS_UID);
        }

        bridge.logger.log("[Unknown interface] " + std::string(iid_string));

        return Steinberg::kNotImplemented;
    }
}

tresult PLUGIN_API
YaPluginFactoryPluginImpl::setHostContext(Steinberg::FUnknown* /*context*/) {
    // TODO: The docs don't clearly specify what this should be doing, but from
    //       what I've seen this is only used to pass a `IHostApplication`
    //       instance. That's used to allow the plugin to create objects in the
    //       host.
    return Steinberg::kNotImplemented;
}
