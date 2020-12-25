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

#include "host-context-proxy.h"

#include <iostream>

#include <public.sdk/source/vst/hosting/hostclasses.h>

Vst3HostContextProxyImpl::Vst3HostContextProxyImpl(
    Vst3Bridge& bridge,
    Vst3HostContextProxy::ConstructArgs&& args)
    : Vst3HostContextProxy(std::move(args)), bridge(bridge) {
    // The lifecycle of this object is managed together with that of the plugin
    // object instance this host context got passed to
}

tresult PLUGIN_API
Vst3HostContextProxyImpl::queryInterface(const Steinberg::TUID _iid,
                                         void** obj) {
    // I don't think it's expected of a host to implement multiple interfaces on
    // this object, so if we do get a call here it's important that it's logged
    // TODO: Successful queries should also be logged
    const tresult result = Vst3HostContextProxy::queryInterface(_iid, obj);
    if (result != Steinberg::kResultOk) {
        std::cerr << "TODO: Implement unknown interface logging on Wine side "
                     "for Vst3HostContextProxyImpl::queryInterface"
                  << std::endl;
    }

    return result;
}

tresult PLUGIN_API
Vst3HostContextProxyImpl::getName(Steinberg::Vst::String128 name) {
    const GetNameResponse response = bridge.send_message(
        YaHostApplication::GetName{.owner_instance_id = owner_instance_id()});

    std::copy(response.name.begin(), response.name.end(), name);
    name[response.name.size()] = 0;

    return response.result;
}

tresult PLUGIN_API
Vst3HostContextProxyImpl::createInstance(Steinberg::TUID /*cid*/,
                                         Steinberg::TUID _iid,
                                         void** obj) {
    // Class IDs don't have a meaning here, they just mirrored the interface
    // from `IPlugFactory::createInstance()`
    constexpr size_t uid_size = sizeof(Steinberg::TUID);
    if (!_iid || !obj || strnlen(_iid, uid_size) < uid_size) {
        return Steinberg::kInvalidArgument;
    }

    // These objects don't have to be created by the actual host since they'll
    // only be used as an argument to other functions. We can just serialize
    // them at that point.
    Steinberg::FUID iid = Steinberg::FUID::fromTUID(_iid);
    if (iid == Steinberg::Vst::IMessage::iid) {
        // TODO: Add logging for this on verbosity level 1
        *obj = static_cast<Steinberg::Vst::IMessage*>(
            new Steinberg::Vst::HostMessage{});
        return Steinberg::kResultTrue;
    } else if (iid == Steinberg::Vst::IAttributeList::iid) {
        // TODO: Add logging for this on verbosity level 1
        *obj = static_cast<Steinberg::Vst::IAttributeList*>(
            new Steinberg::Vst::HostAttributeList{});
        return Steinberg::kResultTrue;
    } else {
        // When the host requests an interface we do not (yet) implement,
        // we'll print a recognizable log message
        const Steinberg::FUID uid = Steinberg::FUID::fromTUID(_iid);
        std::cerr << "TODO: Implement unknown interface logging on Wine side "
                     "for Vst3HostContextProxyImpl::createInstance"
                  << std::endl;

        return Steinberg::kNotImplemented;
    }
}
