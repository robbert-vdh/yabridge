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

#include "boost-fix.h"

#include <future>
#include <memory>
#include <optional>
#include <unordered_set>

#include <windows.h>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>
#include <function2/function2.hpp>

#include "../common/utils.h"

// Forward declaration for use in our watchdog in `MainContext`
class HostBridge;

/**
 * A proxy function that calls `Win32Thread::entry_point` since `CreateThread()`
 * is not usable with lambdas directly. Calling the passed function will invoke
 * the lambda with the arguments passed during `Win32Thread`'s constructor. This
 * function deallocates the function after it's finished executing.
 *
 * We can't store the function pointer in the `Win32Thread` object because
 * moving a `Win32Thread` object would then cause issues.
 *
 * @param entry_point A `fu2::unique_function<void()>*` pointer to a function
 *   pointer, great.
 */
uint32_t WINAPI
win32_thread_trampoline(fu2::unique_function<void()>* entry_point);

/**
 * A simple RAII wrapper around the Win32 thread API that imitates
 * `std::jthread`, including implicit joining (or waiting, since this is Win32)
 * on destruction.
 *
 * `std::thread` uses pthreads directly in Winelib (since this is technically a
 * regular Linux application). This means that when using
 * `std::thread`/`std::jthread` directly, some thread local information that
 * `CreateThread()` would normally set does not get initialized. This could then
 * lead to memory errors. This wrapper aims to be equivalent to `std::jthread`,
 * but using the Win32 API instead.
 *
 * @note This should be used instead of `std::thread` or `std::jthread` whenever
 *   the thread directly calls third party library code, i.e. `LoadLibrary()`,
 *   `FreeLibrary()`, the plugin's entry point, or any of the `AEffect::*()`
 *   functions.
 */
class Win32Thread {
   public:
    /**
     * Constructor that does not start any thread yet.
     */
    Win32Thread() noexcept;

    /**
     * Constructor that immediately starts running the thread. This works
     * equivalently to `std::jthread`.
     *
     * @param entry_point The thread entry point that should be run.
     * @param parameter The parameter passed to the entry point function.
     */
    template <typename Function, typename... Args>
    Win32Thread(Function fn, Args... args)
        : handle_(CreateThread(
                      nullptr,
                      0,
                      reinterpret_cast<LPTHREAD_START_ROUTINE>(
                          win32_thread_trampoline),
                      // `std::function` does not support functions with move
                      // captures the function has to be copy-constructable.
                      // Function2's unique_function lets us capture and move
                      // our arguments to the lambda so we don't end up with
                      // dangling references.
                      new fu2::unique_function<void()>(
                          [f = std::move(fn),
                           ... args = std::move(args)]() mutable {
                              f(std::move(args)...);
                          }),
                      0,
                      nullptr),
                  CloseHandle) {}

    /**
     * Join (or wait on, since this is WIn32) the thread on shutdown, just like
     * `std::jthread` does.
     */
    ~Win32Thread() noexcept;

    Win32Thread(const Win32Thread&) = delete;
    Win32Thread& operator=(const Win32Thread&) = delete;

    Win32Thread(Win32Thread&&) noexcept;
    Win32Thread& operator=(Win32Thread&&) noexcept;

   private:
    // FIXME: This emits `-Wignored-attributes` as of Wine 5.22
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"

    /**
     * The handle for the thread that is running, will be a null pointer if this
     * class was constructed with the default constructor.
     */
    std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(&CloseHandle)>
        handle_;

#pragma GCC diagnostic pop
};

/**
 * A simple RAII wrapper around `SetTimer`. Does not support timer procs since
 * we don't use them.
 */
class Win32Timer {
   public:
    Win32Timer() noexcept;
    Win32Timer(HWND window_handle,
               size_t timer_id,
               unsigned int interval_ms) noexcept;

    ~Win32Timer() noexcept;

    Win32Timer(const Win32Timer&) = delete;
    Win32Timer& operator=(const Win32Timer&) = delete;

    Win32Timer(Win32Timer&&) noexcept;
    Win32Timer& operator=(Win32Timer&&) noexcept;

   private:
    HWND window_handle_;
    std::optional<size_t> timer_id_;
};

/**
 * A wrapper around `boost::asio::io_context()` to serve as the application's
 * main IO context, run from the GUI thread. A single instance is shared for all
 * plugins in a plugin group so that several important events can be handled on
 * the main thread, which can be required because in the Win32 model all GUI
 * related operations have to be handled from the same thread.
 *
 * This also spawns a second IO context in its own thread, which will be used as
 * a watchdog to shutdown a plugin instance's sockets when the process that
 * spawned it is no longer active. This approach also works with plugin groups
 * since closing a plugin's sockets will only cause that one plugin to
 * terminate.
 */
class MainContext {
   public:
    MainContext();

    /**
     * Run the IO context. This rest of this class assumes that this is only
     * done from a single thread.
     */
    void run();

    /**
     * Drop all future work from the IO context. This does not necessarily mean
     * that the thread that called `main_context_.run()` immediatly returns.
     */
    void stop() noexcept;

    /**
     * Set a new timer interval. We'll do this whenever a new plugin loads,
     * because we can't know in advance what the plugin's frame rate option is
     * set to.
     */
    void update_timer_interval(
        std::chrono::steady_clock::duration new_interval) noexcept;

    /**
     * The RAII guard used to register and unregister host bridge instances from
     * our watchdog.
     */
    class WatchdogGuard {
       public:
        WatchdogGuard(HostBridge& bridge,
                      std::unordered_set<HostBridge*>& watched_bridges,
                      std::mutex& watched_bridges_mutex);
        ~WatchdogGuard() noexcept;

        WatchdogGuard(const WatchdogGuard&) = delete;
        WatchdogGuard& operator=(const WatchdogGuard&) = delete;

        WatchdogGuard(WatchdogGuard&& o) noexcept;
        WatchdogGuard& operator=(WatchdogGuard&& o) noexcept;

       private:
        /**
         * Used to facilitate moves.
         */
        bool is_active_ = true;

        /**
         * The bridge that we will add to the watchdog list when this object
         * gets created, and that we'll remove from the list again when this
         * object gets destroyed.
         */
        HostBridge* bridge_;

        // References to the same two fields on `MainContext`, so we don't have
        // to use `friend`
        std::reference_wrapper<std::unordered_set<HostBridge*>>
            watched_bridges_;
        std::reference_wrapper<std::mutex> watched_bridges_mutex_;
    };

    /**
     * Register a bridge instance for our watchdog. We'll periodically check if
     * the remote (native) host process that should be connected to the bridge
     * instance is still alive, and we'll shut down the bridge if it is not to
     * prevent dangling processes. The returned guard should be stored as a
     * field in `HostBridge`, and the watchdog will automatically be
     * unregistered once this guard drops from scope.
     */
    WatchdogGuard register_watchdog(HostBridge& bridge);

    /**
     * Returns `true` if the calling thread is the GUI thread, aka the thread
     * that called `MainContext::run()`.
     */
    inline bool is_gui_thread() const noexcept {
        return GetCurrentThreadId() == gui_thread_id_.value_or(0);
    }

    /**
     * Asynchronously execute a function inside of this main IO context and
     * return the results as a future. This is used to make sure that operations
     * that may involve the Win32 message loop are all run from the same thread.
     */
    template <std::invocable F>
    std::future<std::invoke_result_t<F>> run_in_context(F&& fn) {
        using Result = std::invoke_result_t<F>;

        std::packaged_task<Result()> call_fn(std::forward<F>(fn));
        std::future<Result> result = call_fn.get_future();
        boost::asio::dispatch(context_, std::move(call_fn));

        return result;
    }

    /**
     * Run a task within the IO context. The difference with `run_in_context()`
     * is that this version does not guarantee that it's going to be executed as
     * soon as possible, and thus we also won't return a future.
     */
    template <std::invocable F>
    void schedule_task(F&& fn) {
        boost::asio::post(context_, std::forward<F>(fn));
    }

    /**
     * Start a timer to handle events on a user configurable interval. The
     * interval is controllable through the `frame_rate` option and defaults to
     * 60 updates per second.
     *
     * @param handler The function that should be executed in the IO context
     *   when the timer ticks. This should be a function that handles both the
     *   X11 events and the Win32 message loop.
     * @param predicate A function returning a boolean to indicate whether
     *   `handler` should be run. If this returns `false`, then the current
     *   event loop cycle will be skipped. This is used to prevent the Win32
     *   message loop from being run when there are partially initialized
     *   plugins. So far the VST2 versions of T-RackS 5 are the only plugins
     *   where this has been an issue as those plugins have a race condition
     *   that will cause them to stall indefinitely in this situation, but who
     *   knows which other plugins exert similar behaviour.
     */
    template <std::invocable F, invocable_returning<bool> P>
    void async_handle_events(F handler, P predicate) {
        // Try to keep a steady framerate, but add in delays to let other events
        // get handled if the GUI message handling somehow takes very long.
        events_timer_.expires_at(
            std::max(events_timer_.expiry() + timer_interval_,
                     std::chrono::steady_clock::now() + timer_interval_ / 4));
        events_timer_.async_wait(
            [&, handler, predicate](const boost::system::error_code& error) {
                if (error.failed()) {
                    return;
                }

                if (predicate()) {
                    handler();
                }

                async_handle_events(handler, predicate);
            });
    }

    /**
     * The raw IO context. Used to bind our sockets onto. Running things within
     * this IO context should be done with the functions above.
     */
    boost::asio::io_context context_;

   private:
    /**
     * Start a timer to periodically check whether the host processes belong to
     * all active plugin bridges are still alive. We will shut down the plugin
     * instances where this is not the case, so that this process can gracefully
     * terminate. In some cases Unix Domain Sockets are left in a state where
     * it's impossible to tell that the remote isn't alive anymore, and where
     * `recv()` will just hang indefinitely. We use this watchdog to avoid this.
     */
    void async_handle_watchdog_timer(
        std::chrono::steady_clock::duration interval);

    /**
     * The **Windows** thread ID the context is running on, which will be our
     * GUI thread. Will be a nullopt until `MainContext::run()` has been called.
     */
    std::optional<uint32_t> gui_thread_id_;

    /**
     * The timer used to periodically handle X11 events and Win32 messages.
     */
    boost::asio::steady_timer events_timer_;

    /**
     * The time between timer ticks in `async_handle_events`. This gets
     * initialized at 60 ticks per second, and when a plugin load we'll update
     * this value based on the plugin's `frame_rate` option.
     *
     * @see update_timer_interval
     */
    std::chrono::steady_clock::duration timer_interval_ =
        std::chrono::milliseconds(1000) / 60;

    /**
     * The IO context used for the watchdog described below.
     */
    boost::asio::io_context watchdog_context_;

    /**
     * The timer used to periodically check if the host processes are still
     * active, so we can shut down a plugin's sockets (and with that the plugin
     * itself) when the host has exited and the sockets are somehow not closed
     * yet..
     */
    boost::asio::steady_timer watchdog_timer_;

    /**
     * All of the bridges we're watching as part of our watchdog. We're storing
     * pointers for efficiency's sake, since reference wrappers don't implement
     * any comparison operators.
     */
    std::unordered_set<HostBridge*> watched_bridges_;
    std::mutex watched_bridges_mutex_;

    /**
     * The thread where we run our watchdog timer, to shut down plugins after
     * the native plugin host process they're supposed to be connected to has
     * died.
     */
    Win32Thread watchdog_handler_;
};
