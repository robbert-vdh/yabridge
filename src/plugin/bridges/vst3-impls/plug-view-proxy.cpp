// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2026 Robbert van der Helm
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

#include <asio/post.hpp>

namespace {

/**
 * Protects `run_loop_task_instances` and `gui_wait_contexts`. Locked before an
 * instance's `tasks_mutex_`.
 */
std::mutex run_loop_tasks_registry_mutex;

/**
 * The thread that created the last `RunLoopTasks` instance. `RunLoopTasks` are
 * created from `IPlugView::setFrame()`, which the host calls from its GUI
 * thread.
 */
std::atomic<std::thread::id> host_gui_thread_id;

/**
 * Every live `RunLoopTasks` instance in this process.
 */
std::vector<RunLoopTasks*> run_loop_task_instances;

/**
 * The IO contexts of the mutually recursive calls the host's GUI thread is
 * currently waiting in, innermost last.
 *
 * @see begin_host_gui_thread_wait
 */
std::vector<std::shared_ptr<asio::io_context>> gui_wait_contexts;

}  // namespace

RunLoopTasks::RunLoopTasks(Steinberg::IPtr<Steinberg::IPlugFrame> plug_frame)
    : run_loop_(plug_frame) {
    FUNKNOWN_CTOR

    if (!run_loop_) {
        throw std::runtime_error(
            "The host's 'IPlugFrame' object does not support 'IRunLoop'");
    }

    // This should be backed by eventfd instead, but Ardour doesn't allow that
    std::array<int, 2> sockets;
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0,
                   sockets.data()) != 0) {
        throw std::runtime_error("Failed to create a Unix domain socket");
    }

    socket_read_fd_ = sockets[0];
    socket_write_fd_ = sockets[1];
    if (run_loop_->registerEventHandler(this, socket_read_fd_) !=
        Steinberg::kResultOk) {
        throw std::runtime_error(
            "Failed to register an event handler with the host's run loop");
    }

    std::lock_guard registry_lock(run_loop_tasks_registry_mutex);
    run_loop_task_instances.push_back(this);
    host_gui_thread_id = std::this_thread::get_id();
}

RunLoopTasks::~RunLoopTasks() {
    FUNKNOWN_DTOR

    {
        std::lock_guard registry_lock(run_loop_tasks_registry_mutex);
        run_loop_task_instances.erase(std::find(run_loop_task_instances.begin(),
                                                run_loop_task_instances.end(),
                                                this));
    }

    run_loop_->unregisterEventHandler(this);
    close(socket_read_fd_);
    close(socket_write_fd_);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
IMPLEMENT_FUNKNOWN_METHODS(RunLoopTasks,
                           Steinberg::Linux::IEventHandler,
                           Steinberg::Linux::IEventHandler::iid)
#pragma GCC diagnostic pop

void RunLoopTasks::schedule(fu2::unique_function<void()> task) {
    // The registry lock makes this atomic with `begin_host_gui_thread_wait()`:
    // either the task is pending when the GUI thread starts waiting, or we see
    // the wait here
    std::lock_guard registry_lock(run_loop_tasks_registry_mutex);
    {
        std::lock_guard eventfd_lock(tasks_mutex_);
        tasks_.push_back(std::move(task));

        uint8_t notify_value = 1;
        assert(write(socket_write_fd_, &notify_value, sizeof(notify_value)) ==
               sizeof(notify_value));
    }

    if (!gui_wait_contexts.empty()) {
        asio::post(*gui_wait_contexts.back(), run_all_pending);
    }
}

void RunLoopTasks::run_pending() {
    std::vector<fu2::unique_function<void()>> tasks;
    take_pending(tasks);

    // Run the tasks without holding the mutex: a task may schedule another task
    // from the host's GUI thread, and we don't want to deadlock on that.
    for (auto& task : tasks) {
        task();
    }
}

void RunLoopTasks::run_all_pending() {
    std::vector<fu2::unique_function<void()>> tasks;
    {
        std::lock_guard registry_lock(run_loop_tasks_registry_mutex);
        for (RunLoopTasks* instance : run_loop_task_instances) {
            instance->take_pending(tasks);
        }
    }

    for (auto& task : tasks) {
        task();
    }
}

void RunLoopTasks::take_pending(
    std::vector<fu2::unique_function<void()>>& tasks) {
    std::lock_guard lock(tasks_mutex_);
    for (auto& task : tasks_) {
        tasks.push_back(std::move(task));
    }
    tasks_.clear();

    // There's one notification byte per scheduled task, but drain the socket
    // completely: a leftover byte would make the host treat the socket as
    // readable forever.
    uint8_t notify_value;
    while (read(socket_read_fd_, &notify_value, sizeof(notify_value)) ==
           sizeof(notify_value)) {
    }
}

void PLUGIN_API
RunLoopTasks::onFDIsSet(Steinberg::Linux::FileDescriptor /*fd*/) {
    run_pending();
}

bool is_host_gui_thread() {
    return std::this_thread::get_id() == host_gui_thread_id.load();
}

void begin_host_gui_thread_wait(
    const std::shared_ptr<asio::io_context>& context) {
    if (!is_host_gui_thread()) {
        return;
    }

    std::lock_guard registry_lock(run_loop_tasks_registry_mutex);
    gui_wait_contexts.push_back(context);

    // Tasks scheduled before this point did not see this wait
    asio::post(*context, RunLoopTasks::run_all_pending);
}

void end_host_gui_thread_wait(
    const std::shared_ptr<asio::io_context>& context) {
    // This is called from the thread that sent the message, so we can't check
    // which thread started the wait
    std::lock_guard registry_lock(run_loop_tasks_registry_mutex);
    if (const auto it = std::find(gui_wait_contexts.begin(),
                                  gui_wait_contexts.end(), context);
        it != gui_wait_contexts.end()) {
        gui_wait_contexts.erase(it);
    }
}

Vst3PlugViewProxyImpl::Vst3PlugViewProxyImpl(
    Vst3PluginBridge& bridge,
    Vst3PlugViewProxy::ConstructArgs&& args) noexcept
    : Vst3PlugViewProxy(std::move(args)), bridge_(bridge) {}

Vst3PlugViewProxyImpl::~Vst3PlugViewProxyImpl() noexcept {
    // Also drop the plug view smart pointer on the Wine side when this gets
    // dropped
    // NOTE: This can actually throw (e.g. out of memory or the socket got
    //       closed). But if that were to happen, then we wouldn't be able to
    //       recover from it anyways.
    // NOTE: This also runs pending run loop tasks while we wait, which other
    //       editors hosted by the same Wine process may be blocked on. Hosts
    //       normally call `setFrame(nullptr)` before dropping the view, and
    //       that already removed this view's own task queue.
    bridge_.send_mutually_recursive_message(
        Vst3PlugViewProxy::Destruct{.owner_instance_id = owner_instance_id()});
}

tresult PLUGIN_API
Vst3PlugViewProxyImpl::queryInterface(const Steinberg::TUID _iid, void** obj) {
    const tresult result = Vst3PlugViewProxy::queryInterface(_iid, obj);
    bridge_.logger_.log_query_interface("In IPlugView::queryInterface()",
                                        result,
                                        Steinberg::FUID::fromTUID(_iid));

    return result;
}

tresult PLUGIN_API
Vst3PlugViewProxyImpl::isPlatformTypeSupported(Steinberg::FIDString type) {
    if (type) {
        // We'll swap the X11 window ID platform type string for the Win32 HWND
        // equivalent on the Wine side
        return bridge_.send_mutually_recursive_message(
            YaPlugView::IsPlatformTypeSupported{
                .owner_instance_id = owner_instance_id(), .type = type});
    } else {
        bridge_.logger_.log(
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
        return bridge_.send_mutually_recursive_message(YaPlugView::Attached{
            .owner_instance_id = owner_instance_id(),
            .parent = reinterpret_cast<native_size_t>(parent),
            .type = type});
    } else {
        bridge_.logger_.log(
            "WARNING: Null pointer passed to 'IPlugView::attached()'");
        return Steinberg::kInvalidArgument;
    }
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::removed() {
    return bridge_.send_mutually_recursive_message(
        YaPlugView::Removed{.owner_instance_id = owner_instance_id()});
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::onWheel(float distance) {
    return bridge_.send_mutually_recursive_message(YaPlugView::OnWheel{
        .owner_instance_id = owner_instance_id(), .distance = distance});
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::onKeyDown(char16 key,
                                                    int16 keyCode,
                                                    int16 modifiers) {
    return bridge_.send_mutually_recursive_message(
        YaPlugView::OnKeyDown{.owner_instance_id = owner_instance_id(),
                              .key = key,
                              .key_code = keyCode,
                              .modifiers = modifiers});
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::onKeyUp(char16 key,
                                                  int16 keyCode,
                                                  int16 modifiers) {
    return bridge_.send_mutually_recursive_message(
        YaPlugView::OnKeyUp{.owner_instance_id = owner_instance_id(),
                            .key = key,
                            .key_code = keyCode,
                            .modifiers = modifiers});
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::getSize(Steinberg::ViewRect* size) {
    if (size) {
        const GetSizeResponse response =
            bridge_.send_mutually_recursive_message(
                YaPlugView::GetSize{.owner_instance_id = owner_instance_id()});

        *size = response.size;

        return response.result;
    } else {
        bridge_.logger_.log(
            "WARNING: Null pointer passed to 'IPlugView::getSize()'");
        return Steinberg::kInvalidArgument;
    }
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::onSize(Steinberg::ViewRect* newSize) {
    if (newSize) {
        return bridge_.send_mutually_recursive_message(YaPlugView::OnSize{
            .owner_instance_id = owner_instance_id(), .new_size = *newSize});
    } else {
        bridge_.logger_.log(
            "WARNING: Null pointer passed to 'IPlugView::onSize()'");
        return Steinberg::kInvalidArgument;
    }
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::onFocus(TBool state) {
    return bridge_.send_mutually_recursive_message(YaPlugView::OnFocus{
        .owner_instance_id = owner_instance_id(), .state = state});
}

tresult PLUGIN_API
Vst3PlugViewProxyImpl::setFrame(Steinberg::IPlugFrame* frame) {
    // Null pointers are valid here going from the reference implementations in
    // the SDK
    if (frame) {
        // We'll store the pointer for when the plugin later makes a callback to
        // this component handler
        plug_frame_ = frame;

        // REAPER's GUI is not thread safe, and if we don't call
        // `IPlugFrame::resizeView()` or `IContextMenu::popup()` from a thread
        // owned by REAPER then REAPER will eventually segfault We should thus
        // try to call those functions from an `IRunLoop` event handler.
        try {
            run_loop_tasks_.emplace(plug_frame_);
        } catch (const std::runtime_error& error) {
            // In case the host does not support `IRunLoop` or if we can't
            // register an event handler, we'll throw during `RunLoopTasks`'
            // constructor
            run_loop_tasks_.reset();

            bridge_.logger_.log(
                "The host does not support IRunLoop, falling back to naive GUI "
                "function execution: " +
                std::string(error.what()));
        }

        return bridge_.send_mutually_recursive_message(YaPlugView::SetFrame{
            .owner_instance_id = owner_instance_id(),
            .plug_frame_args = Vst3PlugFrameProxy::ConstructArgs(
                plug_frame_, owner_instance_id())});
    } else {
        plug_frame_.reset();
        run_loop_tasks_.reset();

        return bridge_.send_mutually_recursive_message(
            YaPlugView::SetFrame{.owner_instance_id = owner_instance_id(),
                                 .plug_frame_args = std::nullopt});
    }
}

tresult PLUGIN_API Vst3PlugViewProxyImpl::canResize() {
    const auto request =
        YaPlugView::CanResize{.owner_instance_id = owner_instance_id()};

    {
        std::lock_guard lock(can_resize_cache_mutex_);
        if (const tresult* result = can_resize_cache_.get_and_keep_alive(5)) {
            const bool log_response =
                bridge_.logger_.log_request(true, request);
            if (log_response) {
                bridge_.logger_.log_response(
                    false, YaPlugView::CanResize::Response(*result), true);
            }

            return *result;
        }
    }

    const UniversalTResult result =
        bridge_.send_mutually_recursive_message(request);

    {
        std::lock_guard lock(can_resize_cache_mutex_);
        can_resize_cache_.set(result, 5);
    }

    return result;
}

tresult PLUGIN_API
Vst3PlugViewProxyImpl::checkSizeConstraint(Steinberg::ViewRect* rect) {
    if (rect) {
        const CheckSizeConstraintResponse response =
            bridge_.send_mutually_recursive_message(
                YaPlugView::CheckSizeConstraint{
                    .owner_instance_id = owner_instance_id(), .rect = *rect});

        *rect = response.updated_rect;

        return response.result;
    } else {
        bridge_.logger_.log(
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
        bridge_.send_mutually_recursive_message(
            YaParameterFinder::FindParameter{
                .owner_instance_id = owner_instance_id(),
                .x_pos = xPos,
                .y_pos = yPos});

    resultTag = response.result_tag;

    return response.result;
}

tresult PLUGIN_API
Vst3PlugViewProxyImpl::setContentScaleFactor(ScaleFactor factor) {
    return bridge_.send_mutually_recursive_message(
        YaPlugViewContentScaleSupport::SetContentScaleFactor{
            .owner_instance_id = owner_instance_id(), .factor = factor});
}
