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

#include "plug-frame-proxy.h"

#include <iostream>

Vst3PlugFrameProxyImpl::Vst3PlugFrameProxyImpl(
    Vst3Bridge& bridge,
    Vst3PlugFrameProxy::ConstructArgs&& args)
    : Vst3PlugFrameProxy(std::move(args)), bridge(bridge) {
    // The lifecycle is thos object is managed together with that of the plugin
    // object instance instance this belongs to
}

tresult PLUGIN_API
Vst3PlugFrameProxyImpl::queryInterface(const Steinberg::TUID _iid, void** obj) {
    // TODO: Successful queries should also be logged
    const tresult result = Vst3PlugFrameProxy::queryInterface(_iid, obj);
    if (result != Steinberg::kResultOk) {
        std::cerr << "TODO: Implement unknown interface logging on Wine side "
                     "for Vst3PlugFrameProxyImpl::queryInterface"
                  << std::endl;
    }

    return result;
}

tresult PLUGIN_API
Vst3PlugFrameProxyImpl::resizeView(Steinberg::IPlugView* view,
                                   Steinberg::ViewRect* newSize) {
    // TODO: Implement
    std::cerr << "TODO: IPlugFrame::resizeView()" << std::endl;
    return Steinberg::kNotImplemented;
}
