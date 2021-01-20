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

#pragma once

#include <function2/function2.hpp>

#include "../vst3.h"

#include <boost/asio/dispatch.hpp>

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
     * to be run from there. This works very much like how we use Boost.Asio IO
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
    Steinberg::FUnknownPtr<Steinberg::Linux::IRunLoop> run_loop;

    /**
     * Tasks that should be executed in the next `IRunLoop` event handler call.
     *
     * @relates Vst3PlugViewProxyImpl::run_gui_task
     *
     * @see RunLoopTasks::schedule_task
     */
    std::vector<fu2::unique_function<void()>> tasks;
    std::mutex tasks_mutex;

    /**
     * A dumy Unix domain socket file descriptor used to signal that there is a
     * task ready. We'll pass this to the host's `IRunLoop` so it can tell when
     * we have an event to handle.
     *
     * XXX: This should be backed by eventfd instead, but Ardour doesn't support
     *      that
     */
    int socket_read_fd = -1;
    /**
     * The other side of `socket_read_fd`. We'll write to this when we want the
     * hsot to call our event handler.
     */
    int socket_write_fd = -1;
};

class Vst3PlugViewProxyImpl : public Vst3PlugViewProxy {
   public:
    Vst3PlugViewProxyImpl(Vst3PluginBridge& bridge,
                          Vst3PlugViewProxy::ConstructArgs&& args);

    /**
     * When the reference count reaches zero and this destructor is called,
     * we'll send a request to the Wine plugin host to destroy the corresponding
     * object.
     */
    ~Vst3PlugViewProxyImpl();

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
     * we can post tasks to `run_loop_tasks` instead of executing them directly
     * in `main_context` when no mutually recursive function calls are happening
     * right now.
     *
     * @see send_mutually_recursive_message
     */
    template <typename T, typename F>
    T run_gui_task(F f) {
        std::packaged_task<T()> do_call(std::move(f));
        std::future<T> do_call_response = do_call.get_future();

        // If `send_mutually_recursive_message()` is currently being called
        // (because the host is calling one of `IPlugView`'s methods from its
        // UGI thread) then we'll post a message to an IO context that's
        // currently accepting work on the that thread. Since in theory we could
        // have nested mutual recursion, we need to keep track of a stack of IO
        // contexts. Great. Otherwise we'll schedule the task to be run from an
        // event handler registered to the host's run loop. If the host does not
        // support `IRunLoop`, we'll just run `f` directly.
        {
            std::unique_lock mutual_recursion_lock(
                mutual_recursion_contexts_mutex);
            if (!mutual_recursion_contexts.empty()) {
                boost::asio::dispatch(*mutual_recursion_contexts.back(),
                                      std::move(do_call));
            } else {
                mutual_recursion_lock.unlock();

                if (run_loop_tasks) {
                    run_loop_tasks->schedule(std::move(do_call));
                } else {
                    do_call();
                }
            }
        }

        return do_call_response.get();
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
    Steinberg::IPtr<Steinberg::IPlugFrame> plug_frame;

   private:
    /**
     * Send a message from this `IPlugView` instance. This function will be
     * called by the host on its GUI thread, so until this function returns
     * we'll know that the no `IRunLoop` event handlers will be called. Because
     * of this we'll have to use this function to handling mutually recursive
     * function calls, such as the calling sequence for resizing views. This
     * should be used instead of sending the messages directly.
     *
     * We use the same trick in `Vst3Bridge`.
     */
    template <typename T>
    typename T::Response send_mutually_recursive_message(const T& object) {
        using TResponse = typename T::Response;

        // This IO context will accept incoming calls from `run_gui_task()`
        // until we receive a response. We keep these on a stack as we need to
        // support multiple levels of mutual recursion. This could happen during
        // `IPlugView::attached() -> IPlugFrame::resizeView() ->
        // IPlugView::onSize()`.
        std::shared_ptr<boost::asio::io_context> current_io_context =
            std::make_shared<boost::asio::io_context>();
        {
            std::unique_lock lock(mutual_recursion_contexts_mutex);
            mutual_recursion_contexts.push_back(current_io_context);
        }

        // We will call the function from another thread so we can handle calls
        // to  from this thread
        std::promise<TResponse> response_promise{};
        std::jthread sending_thread([&]() {
            const TResponse response = bridge.send_message(object);

            // Stop accepting additional work to be run from the calling thread
            // once we receive a response
            std::lock_guard lock(mutual_recursion_contexts_mutex);
            current_io_context->stop();
            mutual_recursion_contexts.erase(
                std::find(mutual_recursion_contexts.begin(),
                          mutual_recursion_contexts.end(), current_io_context));

            response_promise.set_value(response);
        });

        // Accept work from the other thread until we receive a response, at
        // which point the context will be stopped
        auto work_guard = boost::asio::make_work_guard(*current_io_context);
        current_io_context->run();

        return response_promise.get_future().get();
    }

    Vst3PluginBridge& bridge;

    /**
     * The IO contexts used in `send_mutually_recursive_message()` to be able to
     * execute functions from that same calling thread while we're waiting for a
     * response. We need an entire stack of these to support mutual recursion,
     * how fun! See the docstring there for more information. When this doesn't
     * contain an IO context, this function is not being called and
     * `run_gui_task()` should post the task to `run_loop_tasks`. This works
     * exactly the same as the mutual recursion handling in `Vst3Bridge`.
     */
    std::vector<std::shared_ptr<boost::asio::io_context>>
        mutual_recursion_contexts;
    std::mutex mutual_recursion_contexts_mutex;

    /**
     * If the host supports `IRunLoop`, we'll use this to run certain tasks from
     * the host's GUI thread using a run loop event handler in
     * `Vst3PlugViewProxyImpl::run_gui_task`.
     *
     * This will be an `std::nullopt` if the hostdoes not support `IRunLoop`.
     */
    std::optional<RunLoopTasks> run_loop_tasks;
};
