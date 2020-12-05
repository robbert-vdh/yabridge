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

YaPluginFactoryPluginImpl::YaPluginFactoryPluginImpl(Vst3PluginBridge& bridge)
    : bridge(bridge) {}

tresult PLUGIN_API
YaPluginFactoryPluginImpl::createInstance(Steinberg::FIDString /*cid*/,
                                          Steinberg::FIDString /*_iid*/,
                                          void** /*obj*/) {
    // TODO: Send a control message
    return 0;
}

tresult PLUGIN_API
YaPluginFactoryPluginImpl::setHostContext(Steinberg::FUnknown* /*context*/) {
    // TODO: Send a control message
    return 0;
}
