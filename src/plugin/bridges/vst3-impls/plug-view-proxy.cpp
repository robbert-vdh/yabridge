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

#include "plug-view-proxy.h"

Vst3PlugViewProxyImpl::Vst3PlugViewProxyImpl(
    Vst3PluginBridge& bridge,
    Vst3PlugViewProxy::ConstructArgs&& args)
    : Vst3PlugViewProxy(std::move(args)), bridge(bridge) {}

Vst3PlugViewProxyImpl::~Vst3PlugViewProxyImpl() {
    // Also drop the plug view smart pointer on the Wine side when this gets
    // dropped
    bridge.send_message(
        Vst3PlugViewProxy::Destruct{.owner_instance_id = owner_instance_id()});
}

tresult PLUGIN_API
Vst3PlugViewProxyImpl::queryInterface(const Steinberg::TUID _iid, void** obj) {
    // TODO: Successful queries should also be logged
    const tresult result = Vst3PlugViewProxy::queryInterface(_iid, obj);
    if (result != Steinberg::kResultOk) {
        bridge.logger.log_unknown_interface("In IPlugView::queryInterface()",
                                            Steinberg::FUID::fromTUID(_iid));
    }

    return result;
}

tresult PLUGIN_API
Vst3PlugViewProxyImpl::isPlatformTypeSupported(Steinberg::FIDString type) {
    // We'll swap the X11 window ID platform type string for the Win32 HWND
    // equivalent on the Wine side
    return bridge.send_message(YaPlugView::IsPlatformTypeSupported{
        .owner_instance_id = owner_instance_id(), .type = type});
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::attached(void* parent,
                                                   Steinberg::FIDString type) {
    // We will embed the Wine Win32 window into the X11 window provided by the
    // host
    return bridge.send_message(
        YaPlugView::Attached{.owner_instance_id = owner_instance_id(),
                             .parent = reinterpret_cast<native_size_t>(parent),
                             .type = type});
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::removed() {
    // TODO: Implement
    bridge.logger.log("TODO: IPlugView::removed()");
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::onWheel(float distance) {
    // TODO: Implement
    bridge.logger.log("TODO: IPlugView::onWheel()");
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::onKeyDown(char16 key,
                                                    int16 keyCode,
                                                    int16 modifiers) {
    // TODO: Implement
    bridge.logger.log("TODO: IPlugView::onKeyDown()");
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::onKeyUp(char16 key,
                                                  int16 keyCode,
                                                  int16 modifiers) {
    // TODO: Implement
    bridge.logger.log("TODO: IPlugView::onKeyUp()");
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::getSize(Steinberg::ViewRect* size) {
    if (size) {
        const GetSizeResponse response =
            bridge.send_message(YaPlugView::GetSize{
                .owner_instance_id = owner_instance_id(), .size = *size});

        *size = response.updated_size;

        return response.result;
    } else {
        bridge.logger.log(
            "WARNING: Null pointer passed to 'IPlugView::getSize()'");
        return Steinberg::kInvalidArgument;
    }
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::onSize(Steinberg::ViewRect* newSize) {
    // TODO: Implement
    bridge.logger.log("TODO: IPlugView::onSize()");
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::onFocus(TBool state) {
    // TODO: Implement
    bridge.logger.log("TODO: IPlugView::onFocus()");
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API
Vst3PlugViewProxyImpl::setFrame(Steinberg::IPlugFrame* frame) {
    // TODO: Implement
    bridge.logger.log("TODO: IPlugView::setFrame()");
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::canResize() {
    // TODO: Implement
    bridge.logger.log("TODO: IPlugView::canResize()");
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API
Vst3PlugViewProxyImpl::checkSizeConstraint(Steinberg::ViewRect* rect) {
    // TODO: Implement
    bridge.logger.log("TODO: IPlugView::checkSizeConstraint()");
    return Steinberg::kNotImplemented;
}
