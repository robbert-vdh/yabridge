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

#include "plug-view-proxy.h"

RunLoopTasks::RunLoopTasks(Steinberg::IPtr<Steinberg::IPlugFrame> plug_frame)
    : run_loop(plug_frame) {
    FUNKNOWN_CTOR

    if (!run_loop) {
        throw std::runtime_error(
            "The host's 'IPlugFrame' object does not support 'IRunLoop'");
    }

    // This should be backed by eventfd instead, but Ardour doesn't allow that
    std::array<int, 2> sockets;
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0,
                   sockets.data()) != 0) {
        throw std::runtime_error("Failed to create a Unix domain socket");
    }

    socket_read_fd = sockets[0];
    socket_write_fd = sockets[1];
    if (run_loop->registerEventHandler(this, socket_read_fd) !=
        Steinberg::kResultOk) {
        throw std::runtime_error(
            "Failed to register an event handler with the host's run loop");
    }
}

RunLoopTasks::~RunLoopTasks() {
    FUNKNOWN_DTOR

    run_loop->unregisterEventHandler(this);
    close(socket_read_fd);
    close(socket_write_fd);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
IMPLEMENT_FUNKNOWN_METHODS(RunLoopTasks,
                           Steinberg::Linux::IEventHandler,
                           Steinberg::Linux::IEventHandler::iid)
#pragma GCC diagnostic pop

void RunLoopTasks::schedule(fu2::unique_function<void()> task) {
    std::lock_guard eventfd_lock(tasks_mutex);
    tasks.push_back(std::move(task));

    uint8_t notify_value = 1;
    write(socket_write_fd, &notify_value, sizeof(notify_value));
}

void PLUGIN_API
RunLoopTasks::onFDIsSet(Steinberg::Linux::FileDescriptor /*fd*/) {
    std::lock_guard lock(tasks_mutex);

    // Run all tasks that have been submitted to our queue from the host's
    // calling thread (which will be the GUI thread)
    for (auto& task : tasks) {
        task();

        // This should in theory stop the host from calling this function, but
        // REAPER doesn't care. And funnily enough we only have to do all of
        // this because of REAPER.
        uint8_t notify_value;
        read(socket_read_fd, &notify_value, sizeof(notify_value));
    }

    tasks.clear();
}

Vst3PlugViewProxyImpl::Vst3PlugViewProxyImpl(
    Vst3PluginBridge& bridge,
    Vst3PlugViewProxy::ConstructArgs&& args)
    : Vst3PlugViewProxy(std::move(args)), bridge(bridge) {}

Vst3PlugViewProxyImpl::~Vst3PlugViewProxyImpl() {
    // Also drop the plug view smart pointer on the Wine side when this gets
    // dropped
    send_mutually_recursive_message(
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
    if (type) {
        // We'll swap the X11 window ID platform type string for the Win32 HWND
        // equivalent on the Wine side
        return send_mutually_recursive_message(
            YaPlugView::IsPlatformTypeSupported{
                .owner_instance_id = owner_instance_id(), .type = type});
    } else {
        bridge.logger.log(
            "WARNING: Null pointer passed to "
            "'IPlugView::isPlatformTypeSupported()'");
        return Steinberg::kInvalidArgument;
    }
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::attached(void* parent,
                                                   Steinberg::FIDString type) {
    if (parent && type) {
        // We will embed the Wine Win32 window into the X11 window provided by
        // the host
        return send_mutually_recursive_message(YaPlugView::Attached{
            .owner_instance_id = owner_instance_id(),
            .parent = reinterpret_cast<native_size_t>(parent),
            .type = type});
    } else {
        bridge.logger.log(
            "WARNING: Null pointer passed to 'IPlugView::attached()'");
        return Steinberg::kInvalidArgument;
    }
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::removed() {
    return send_mutually_recursive_message(
        YaPlugView::Removed{.owner_instance_id = owner_instance_id()});
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::onWheel(float distance) {
    return send_mutually_recursive_message(YaPlugView::OnWheel{
        .owner_instance_id = owner_instance_id(), .distance = distance});
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::onKeyDown(char16 key,
                                                    int16 keyCode,
                                                    int16 modifiers) {
    return send_mutually_recursive_message(
        YaPlugView::OnKeyDown{.owner_instance_id = owner_instance_id(),
                              .key = key,
                              .key_code = keyCode,
                              .modifiers = modifiers});
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::onKeyUp(char16 key,
                                                  int16 keyCode,
                                                  int16 modifiers) {
    return send_mutually_recursive_message(
        YaPlugView::OnKeyUp{.owner_instance_id = owner_instance_id(),
                            .key = key,
                            .key_code = keyCode,
                            .modifiers = modifiers});
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::getSize(Steinberg::ViewRect* size) {
    if (size) {
        const GetSizeResponse response =
            send_mutually_recursive_message(YaPlugView::GetSize{
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
        return send_mutually_recursive_message(YaPlugView::OnSize{
            .owner_instance_id = owner_instance_id(), .new_size = *newSize});
    } else {
        bridge.logger.log(
            "WARNING: Null pointer passed to 'IPlugView::onSize()'");
        return Steinberg::kInvalidArgument;
    }
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::onFocus(TBool state) {
    return send_mutually_recursive_message(YaPlugView::OnFocus{
        .owner_instance_id = owner_instance_id(), .state = state});
}

tresult PLUGIN_API
Vst3PlugViewProxyImpl::setFrame(Steinberg::IPlugFrame* frame) {
    // TODO: Null pointers are valid here, should we pass them through?
    if (frame) {
        // We'll store the pointer for when the plugin later makes a callback to
        // this component handler
        plug_frame = frame;

        // REAPER's GUI is not thread safe, and if we don't call
        // `IPlugFrame::resizeView()` or `IContextMenu::popup()` from a thread
        // owned by REAPER then REAPER will eventually segfault We should thus
        // try to call those functions from an `IRunLoop` event handler.
        try {
            run_loop_tasks.emplace(plug_frame);
        } catch (const std::runtime_error& e) {
            // In case the host does not support `IRunLoop` or if we can't
            // register an event handler, we'll throw during `RunLoopTasks`'
            // constructor
            run_loop_tasks.reset();

            bridge.logger.log(
                "The host does not support IRunLoop, falling back to naive GUI "
                "function execution: " +
                std::string(e.what()));
        }

        return send_mutually_recursive_message(YaPlugView::SetFrame{
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
    return send_mutually_recursive_message(
        YaPlugView::CanResize{.owner_instance_id = owner_instance_id()});
}

tresult PLUGIN_API
Vst3PlugViewProxyImpl::checkSizeConstraint(Steinberg::ViewRect* rect) {
    if (rect) {
        const CheckSizeConstraintResponse response =
            send_mutually_recursive_message(YaPlugView::CheckSizeConstraint{
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

tresult PLUGIN_API Vst3PlugViewProxyImpl::findParameter(
    int32 xPos,
    int32 yPos,
    Steinberg::Vst::ParamID& resultTag /*out*/) {
    const FindParameterResponse response =
        send_mutually_recursive_message(YaParameterFinder::FindParameter{
            .owner_instance_id = owner_instance_id(),
            .x_pos = xPos,
            .y_pos = yPos});

    resultTag = response.result_tag;

    return response.result;
}

tresult PLUGIN_API
Vst3PlugViewProxyImpl::setContentScaleFactor(ScaleFactor factor) {
    return send_mutually_recursive_message(
        YaPlugViewContentScaleSupport::SetContentScaleFactor{
            .owner_instance_id = owner_instance_id(), .factor = factor});
}
