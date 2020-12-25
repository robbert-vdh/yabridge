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

#include "connection-point-proxy.h"

#include <iostream>

Vst3ConnectionPointProxyImpl::Vst3ConnectionPointProxyImpl(
    Vst3Bridge& bridge,
    Vst3ConnectionPointProxy::ConstructArgs&& args)
    : Vst3ConnectionPointProxy(std::move(args)), bridge(bridge) {}

tresult PLUGIN_API
Vst3ConnectionPointProxyImpl::queryInterface(const Steinberg::TUID _iid,
                                             void** obj) {
    // TODO: Successful queries should also be logged
    const tresult result = Vst3ConnectionPointProxy::queryInterface(_iid, obj);
    if (result != Steinberg::kResultOk) {
        std::cerr << "TODO: Implement unknown interface logging on Wine side "
                     "for Vst3ConnectionPointProxyImpl::queryInterface"
                  << std::endl;
    }

    return result;
}

tresult PLUGIN_API
Vst3ConnectionPointProxyImpl::connect(IConnectionPoint* /*other*/) {
    std::cerr << "WARNING: The plugin called IConnectionPoint::connect(), this "
                 "should not happen"
              << std::endl;
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API
Vst3ConnectionPointProxyImpl::disconnect(IConnectionPoint* /*other*/) {
    std::cerr << "WARNING: The plugin called IConnectionPoint::disconnect(), "
                 "this should not happen"
              << std::endl;
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API
Vst3ConnectionPointProxyImpl::notify(Steinberg::Vst::IMessage* message) {
    // As explained in `YaMessage` and `Vst3PluginProxyImpl::notify`, we can
    // only support our own `IMessage implementation here`
    if (auto message_impl = dynamic_cast<YaMessage*>(message)) {
        return bridge.send_message(YaConnectionPoint::Notify{
            .instance_id = owner_instance_id(), .message = *message_impl});
    } else {
        std::cerr << "WARNING: Unknown message type passed to "
                     "'IConnectionPoint::notify()', ignoring"
                  << std::endl;
        return Steinberg::kNotImplemented;
    }
}
