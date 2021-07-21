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

#include "plug-frame-proxy.h"

#include <iostream>

Vst3PlugFrameProxyImpl::Vst3PlugFrameProxyImpl(
    Vst3Bridge& bridge,
    Vst3PlugFrameProxy::ConstructArgs&& args) noexcept
    : Vst3PlugFrameProxy(std::move(args)), bridge(bridge) {
    // The lifecycle of this object is managed together with that of the plugin
    // object instance this host context got passed to
}

tresult PLUGIN_API
Vst3PlugFrameProxyImpl::queryInterface(const Steinberg::TUID _iid, void** obj) {
    const tresult result = Vst3PlugFrameProxy::queryInterface(_iid, obj);
    bridge.logger.log_query_interface("In IPlugFrame::queryInterface()", result,
                                      Steinberg::FUID::fromTUID(_iid));

    return result;
}

tresult PLUGIN_API
Vst3PlugFrameProxyImpl::resizeView(Steinberg::IPlugView* /*view*/,
                                   Steinberg::ViewRect* newSize) {
    if (newSize) {
        // XXX: Since VST3 currently only support a single view type we'll
        //      assume `view` is the `IPlugView*` returned by the last call to
        //      `IEditController::createView()`

        // Resize the editor wrapper window in advance. We will do another
        // resize automatically on `IPlugView::onSize()`, but this should make
        // resizes look a bit smoother.
        bridge.maybe_resize_editor(owner_instance_id(), *newSize);

        // We have to use this special sending function here so we can handle
        // calls to `IPlugView::onSize()` from this same thread (the UI thread).
        // See the docstring for more information.
        return bridge.send_mutually_recursive_message(YaPlugFrame::ResizeView{
            .owner_instance_id = owner_instance_id(), .new_size = *newSize});
    } else {
        std::cerr
            << "WARNING: Null pointer passed to 'IPlugFrame::resizeView()'"
            << std::endl;
        return Steinberg::kInvalidArgument;
    }
}
