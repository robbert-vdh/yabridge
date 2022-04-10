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

#pragma once

#include <function2/function2.hpp>

#include "../vst3.h"

/**
 * A RAII wrapper around `IRunLoop`'s event handlers so we can schedule tasks to
 * be run in it. This is needed for REAPER, because function calls that involve
 * GUI drawing (notable `IPlugFrame::resizeView()` and `IContextMenu::popup()`)
 * have to be run from a thread owned by REAPER. If we don't do this, the
 * `IPlugFrame::resizeView()` won't resize the actual window and both of these
 * functions will eventually cause REAPER to segfault.
 */
class RunLoopTasks : public Steinberg::Linux::IEventHandler {
   public:
    /**
     * Register an event handler in the host's run loop so we can schedule tasks
     * to be run from there. This works very much like how we use Asio IO
     * contexts everywhere else to run functions on other threads. All of this
     * is backed by a dummy Unix domain socket, although REAPER will call the
     * event handler regardless of whether the file descriptor is ready or not.
     * eventfd would have made much more sense here, but Ardour doesn't support
     * that.
     *
     * @throw std::runtime_error If the host does not support
     *   `Steinberg::Linux::IRunLoop`, or if registering the event handler was
     *   not successful. The caller should catch this and call back to not
     *   relying on the run loop.
     */
    RunLoopTasks(Steinberg::IPtr<Steinberg::IPlugFrame> plug_frame);

    /**
     * Unregister the event handler and close the file descriptor on cleanup.
     */
    ~RunLoopTasks();

    DECLARE_FUNKNOWN_METHODS

    /**
     * Schedule a task to be run from the host's GUI thread in an `IRunLoop`
     * event handler. This may block if the host is currently calling
     * `onFDIsSet()`.
     *
     * @param task The task to execute. This can be used with
     *   `std::packaged_task` to run a computation that returns a value from the
     *   host's GUI thread.
     */
    void schedule(fu2::unique_function<void()> task);

    // From `IEventHandler`, required for REAPER because its GUI is not thread
    // safe
    void PLUGIN_API onFDIsSet(Steinberg::Linux::FileDescriptor fd) override;

   private:
    /**
     * This pointer is cast from `plug_frame` once `IPlugView::setFrame()` has
     * been called.
     */
    Steinberg::FUnknownPtr<Steinberg::Linux::IRunLoop> run_loop_;

    /**
     * Tasks that should be executed in the next `IRunLoop` event handler call.
     *
     * @relates Vst3PlugViewProxyImpl::run_gui_task
     *
     * @see RunLoopTasks::schedule
     */
    std::vector<fu2::unique_function<void()>> tasks_;
    std::mutex tasks_mutex_;

    /**
     * A dumy Unix domain socket file descriptor used to signal that there is a
     * task ready. We'll pass this to the host's `IRunLoop` so it can tell when
     * we have an event to handle.
     *
     * XXX: This should be backed by eventfd instead, but Ardour doesn't support
     *      that
     */
    int socket_read_fd_ = -1;
    /**
     * The other side of `socket_read_fd`. We'll write to this when we want the
     * hsot to call our event handler.
     */
    int socket_write_fd_ = -1;
};

class Vst3PlugViewProxyImpl : public Vst3PlugViewProxy {
   public:
    Vst3PlugViewProxyImpl(Vst3PluginBridge& bridge,
                          Vst3PlugViewProxy::ConstructArgs&& args) noexcept;

    /**
     * When the reference count reaches zero and this destructor is called,
     * we'll send a request to the Wine plugin host to destroy the corresponding
     * object.
     */
    ~Vst3PlugViewProxyImpl() noexcept override;

    /**
     * We'll override the query interface to log queries for interfaces we do
     * not (yet) support.
     */
    tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                      void** obj) override;

    /**
     * Run a task that's supposed to be run from the GUI thread.
     * `IPlugFrame::resizeView()` and `IContextMenu::popup()` are the likely
     * candidates here. This is needed for REAPER, as REAPER will segfault if
     * you run those functions from a thread that's not owned by REAPER itself.
     * If the `IPlugFrame` object passed to `IPlugView::setFrame()` supports
     * `IRunLoop`, then we'll schedule `f` to be run from an even handler in the
     * host's run loop. Otherwise `f` is run directly.
     *
     * This works similarly to
     * `Vst3Bridge::do_mutual_recursion_or_handle_in_main_context`, except that
     * we can post tasks to `run_loop_tasks_` instead of executing them directly
     * in `main_context_` when no mutually recursive function calls are
     * happening right now.
     *
     * @see Vst3HostBridge::send_mutually_recursive_message
     */
    template <std::invocable F>
    std::invoke_result_t<F> run_gui_task(F&& fn) {
        using Result = std::invoke_result_t<F>;

        // If `Vst3Bridge::send_mutually_recursive_message()` is currently being
        // called (because the host is calling one of `IPlugView`'s methods from
        // its UGI thread), then we'll call `fn` from that same thread.
        // Otherwise we'll schedule the task to be run from an event handler
        // registered to the host's run loop, if that exists. Finally if the
        // host does not support `IRunLoop`, then we'll just run `fn` directly.
        if (const auto result =
                bridge_.maybe_run_on_mutual_recursion_thread(fn)) {
            return *result;
        }

        if (run_loop_tasks_) {
            std::packaged_task<Result()> do_call(std::forward<F>(fn));
            std::future<Result> do_call_response = do_call.get_future();

            run_loop_tasks_->schedule(std::move(do_call));

            return do_call_response.get();
        } else {
            return fn();
        }
    }

    // From `IPlugView`
    tresult PLUGIN_API
    isPlatformTypeSupported(Steinberg::FIDString type) override;
    tresult PLUGIN_API attached(void* parent,
                                Steinberg::FIDString type) override;
    tresult PLUGIN_API removed() override;
    tresult PLUGIN_API onWheel(float distance) override;
    tresult PLUGIN_API onKeyDown(char16 key,
                                 int16 keyCode,
                                 int16 modifiers) override;
    tresult PLUGIN_API onKeyUp(char16 key,
                               int16 keyCode,
                               int16 modifiers) override;
    tresult PLUGIN_API getSize(Steinberg::ViewRect* size) override;
    tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) override;
    tresult PLUGIN_API onFocus(TBool state) override;
    tresult PLUGIN_API setFrame(Steinberg::IPlugFrame* frame) override;
    tresult PLUGIN_API canResize() override;
    tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* rect) override;

    // From `IParameterFinder`
    tresult PLUGIN_API
    findParameter(int32 xPos,
                  int32 yPos,
                  Steinberg::Vst::ParamID& resultTag /*out*/) override;

    // From `IPlugViewContentScaleSupport`
    tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override;

    /**
     * The `IPlugFrame` object passed by the host passed to us in
     * `IPlugView::setFrame()`. When the plugin makes a callback on the
     * `IPlugFrame` proxy object, we'll pass the call through to this object.
     */
    Steinberg::IPtr<Steinberg::IPlugFrame> plug_frame_;

   private:
    Vst3PluginBridge& bridge_;

    /**
     * If the host supports `IRunLoop`, we'll use this to run certain tasks from
     * the host's GUI thread using a run loop event handler in
     * `Vst3PlugViewProxyImpl::run_gui_task`.
     *
     * This will be an `std::nullopt` if the hostdoes not support `IRunLoop`.
     */
    std::optional<RunLoopTasks> run_loop_tasks_;

    // Caches

    /**
     * During resizing the host will likely constantly ask the plugin if it can
     * be freely resized. Even if it is technically possible, I'm not aware of
     * any plugins that change from not being able arbitrarily resizeable to
     * being able to be resized like this. The reason why we might want to cache
     * `IPlugView::canResize()` is because this function has to be run on the
     * GUI thread, just like `IPlugView::onSize()` and
     * `IPlugView::checkSizeConstraint`. Everything running in lockstep makes
     * resizing a lot laggier than they would have to be, so as a compromise
     * we'll remember this value for the duration of the resize.
     */
    TimedValueCache<tresult> can_resize_cache_;
    std::mutex can_resize_cache_mutex_;
};
