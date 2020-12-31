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
    const tresult result = Vst3PlugViewProxy::queryInterface(_iid, obj);
    bridge.logger.log_query_interface("In IPlugView::queryInterface()", result,
                                      Steinberg::FUID::fromTUID(_iid));

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
    return bridge.send_message(
        YaPlugView::Removed{.owner_instance_id = owner_instance_id()});
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::onWheel(float distance) {
    return bridge.send_message(YaPlugView::OnWheel{
        .owner_instance_id = owner_instance_id(), .distance = distance});
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::onKeyDown(char16 key,
                                                    int16 keyCode,
                                                    int16 modifiers) {
    return bridge.send_message(
        YaPlugView::OnKeyDown{.owner_instance_id = owner_instance_id(),
                              .key = key,
                              .key_code = keyCode,
                              .modifiers = modifiers});
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::onKeyUp(char16 key,
                                                  int16 keyCode,
                                                  int16 modifiers) {
    return bridge.send_message(
        YaPlugView::OnKeyUp{.owner_instance_id = owner_instance_id(),
                            .key = key,
                            .key_code = keyCode,
                            .modifiers = modifiers});
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
    if (newSize) {
        return bridge.send_message(YaPlugView::OnSize{
            .owner_instance_id = owner_instance_id(), .new_size = *newSize});
    } else {
        bridge.logger.log(
            "WARNING: Null pointer passed to 'IPlugView::onSize()'");
        return Steinberg::kInvalidArgument;
    }
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::onFocus(TBool state) {
    return bridge.send_message(YaPlugView::OnFocus{
        .owner_instance_id = owner_instance_id(), .state = state});
}

tresult PLUGIN_API
Vst3PlugViewProxyImpl::setFrame(Steinberg::IPlugFrame* frame) {
    if (frame) {
        // We'll store the pointer for when the plugin later makes a callback to
        // this component handler
        plug_frame = frame;

        return bridge.send_message(YaPlugView::SetFrame{
            .owner_instance_id = owner_instance_id(),
            .plug_frame_args = Vst3PlugFrameProxy::ConstructArgs(
                plug_frame, owner_instance_id())});
    } else {
        bridge.logger.log(
            "WARNING: Null pointer passed to 'IPlugView::setFrame()'");
        return Steinberg::kInvalidArgument;
    }
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::canResize() {
    return bridge.send_message(
        YaPlugView::CanResize{.owner_instance_id = owner_instance_id()});
}

tresult PLUGIN_API
Vst3PlugViewProxyImpl::checkSizeConstraint(Steinberg::ViewRect* rect) {
    if (rect) {
        const CheckSizeConstraintResponse response =
            bridge.send_message(YaPlugView::CheckSizeConstraint{
                .owner_instance_id = owner_instance_id(), .rect = *rect});

        *rect = response.updated_rect;

        return response.result;
    } else {
        bridge.logger.log(
            "WARNING: Null pointer passed to "
            "'IPlugView::checkSizeConstraint()'");
        return Steinberg::kInvalidArgument;
    }
}
