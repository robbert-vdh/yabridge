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

#include "connection-point-proxy.h"

#include <iostream>

Vst3ConnectionPointProxyImpl::Vst3ConnectionPointProxyImpl(
    Vst3Bridge& bridge,
    Vst3ConnectionPointProxy::ConstructArgs&& args) noexcept
    : Vst3ConnectionPointProxy(std::move(args)), bridge_(bridge) {}

tresult PLUGIN_API
Vst3ConnectionPointProxyImpl::queryInterface(const Steinberg::TUID _iid,
                                             void** obj) {
    const tresult result = Vst3ConnectionPointProxy::queryInterface(_iid, obj);
    bridge_.logger_.log_query_interface("In IConnectionPoint::queryInterface()",
                                        result,
                                        Steinberg::FUID::fromTUID(_iid));

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
    if (message) {
        // FabFilter plugins require this to be done from the GUI thread so we
        // need to use our mutual recursion mechanism. Luckily only Ardour uses
        // connection proxies, so if this ends up breaking something it will
        // only affect Ardour.
        return bridge_.send_mutually_recursive_message(
            YaConnectionPoint::Notify{.instance_id = owner_instance_id(),
                                      .message_ptr = YaMessagePtr(*message)});
    } else {
        std::cerr << "WARNING: Null pointer passed to "
                     "'IConnectionPoint::notify()', ignoring"
                  << std::endl;
        return Steinberg::kInvalidArgument;
    }
}
