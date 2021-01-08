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

#include "host-context-proxy.h"

#include <iostream>

#include "../../../common/serialization/vst3/attribute-list.h"
#include "../../../common/serialization/vst3/message.h"

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
    const tresult result = Vst3HostContextProxy::queryInterface(_iid, obj);
    bridge.logger.log_query_interface("In FUnknown::queryInterface()", result,
                                      Steinberg::FUID::fromTUID(_iid));

    return result;
}

tresult PLUGIN_API
Vst3HostContextProxyImpl::getName(Steinberg::Vst::String128 name) {
    if (name) {
        const GetNameResponse response =
            bridge.send_message(YaHostApplication::GetName{
                .owner_instance_id = owner_instance_id()});

        std::copy(response.name.begin(), response.name.end(), name);
        name[response.name.size()] = 0;

        return response.result;
    } else {
        bridge.logger.log(
            "WARNING: Null pointer passed to 'IHostApplication::getName()'");
        return Steinberg::kInvalidArgument;
    }
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
    tresult response;
    Steinberg::FUID iid = Steinberg::FUID::fromTUID(_iid);
    if (iid == Steinberg::Vst::IMessage::iid) {
        *obj = static_cast<Steinberg::Vst::IMessage*>(new YaMessage{});
        response = Steinberg::kResultTrue;
    } else if (iid == Steinberg::Vst::IAttributeList::iid) {
        *obj =
            static_cast<Steinberg::Vst::IAttributeList*>(new YaAttributeList{});
        response = Steinberg::kResultTrue;
    } else {
        *obj = nullptr;
        response = Steinberg::kNotImplemented;
    }

    const Steinberg::FUID uid = Steinberg::FUID::fromTUID(_iid);
    bridge.logger.log_query_interface("In IHostApplication::createInstance()",
                                      response, uid);

    return response;
}
