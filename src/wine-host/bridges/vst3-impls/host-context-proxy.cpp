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

Vst3HostContextProxyImpl::Vst3HostContextProxyImpl(
    Vst3Bridge& bridge,
    Vst3HostContextProxy::ConstructArgs&& args)
    : Vst3HostContextProxy(std::move(args)), bridge(bridge) {
    // The lifecycle is thos object is managed together with that of the plugin
    // object instance instance this belongs to
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
Vst3HostContextProxyImpl::createInstance(Steinberg::TUID cid,
                                         Steinberg::TUID _iid,
                                         void** obj) {
    // TODO: Implement
    std::cerr << "TODO: IHostApplication::createInstance()" << std::endl;
    return Steinberg::kNotImplemented;
}
