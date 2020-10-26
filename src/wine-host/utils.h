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

#pragma once

#include "boost-fix.h"

#include <memory>
#include <optional>

#define NOMINMAX
#define NOSERVICE
#define NOMCX
#define NOIMM
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <boost/asio/io_context.hpp>

/**
 * The delay between calls to the event loop at an even more than cinematic 30
 * fps.
 */
constexpr std::chrono::duration event_loop_interval =
    std::chrono::milliseconds(1000) / 30;

/**
 * A wrapper around `boost::asio::io_context()`. A single instance is shared for
 * all plugins in a plugin group so that most events can be handled on the main
 * thread, which can be required because all GUI related operations have to be
 * handled from the same thread. If during the Win32 message loop the plugin
 * performs a host callback and the host then calls a function on the plugin in
 * response, then this IO context will still be busy with the message loop
 * which. To prevent a deadlock in this situation, we'll allow different threads
 * to handle `dispatch()` calls while the message loop is running.
 */
class PluginContext {
   public:
    PluginContext();

    /**
     * Run the IO context. This rest of this class assumes that this is only
     * done from a single thread.
     */
    void run();

    /**
     * Drop all future work from the IO context. This does not necessarily mean
     * that the thread that called `plugin_context.run()` immediatly returns.
     */
    void stop();

    /**
     * Start a timer to handle events every `event_loop_interval` milliseconds.
     * `message_loop_active()` will return `true` while `handler` is being
     * executed.
     *
     * @param handler The function that should be executed in the IO context
     *   when the timer ticks. This should be a function that handles both the
     *   X11 events and the Win32 message loop.
     */
    template <typename F>
    void async_handle_events(F handler) {
        // Try to keep a steady framerate, but add in delays to let other events
        // get handled if the GUI message handling somehow takes very long.
        events_timer.expires_at(std::max(
            events_timer.expiry() + event_loop_interval,
            std::chrono::steady_clock::now() + std::chrono::milliseconds(5)));
        events_timer.async_wait(
            [&, handler](const boost::system::error_code& error) {
                if (error.failed()) {
                    return;
                }

                event_loop_active = true;
                handler();
                event_loop_active = false;

                async_handle_events(handler);
            });
    }

    /**
     * Is `true` if the context is currently handling the Win32 message loop and
     * incoming `dispatch()` events should be handled on their own thread (as
     * posting them to the IO context will thus block).
     */
    std::atomic_bool event_loop_active;

    /**
     * The raw IO context. Can and should be used directly for everything that's
     * not the event handling loop.
     */
    boost::asio::io_context context;

   private:
    /**
     * The timer used to periodically handle X11 events and Win32 messages.
     */
    boost::asio::steady_timer events_timer;
};

/**
 * A simple RAII wrapper around the Win32 thread API.
 *
 * These threads are implemented using `CreateThread` rather than `std::thread`
 * because in some cases `std::thread` in winelib causes very hard to debug data
 * races within plugins such as Serum. This might be caused by calling
 * conventions being handled differently.
 *
 * This somewhat mimicks `std::thread`, with the following differences:
 *
 * - The threads will immediatly be killed silently when a `Win32Thread` object
 *   goes out of scope. This is the desired behavior in our case since the host
 *   will have already saved chunk data before closing the plugin and this
 *   ensures that the plugin shuts down quickly.
 * - This does not accept lambdas because we're calling a C function that
 *   expects a function pointer of type `LPTHREAD_START_ROUTINE`. GCC supports
 *   converting stateless lambdas to this format, but clang (as used for IDE
 *   tooling) does not.
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
    Win32Thread();

    /**
     * Constructor that immediately starts running the thread
     *
     * @param entry_point The thread entry point that should be run.
     * @param parameter The parameter passed to the entry point function.

     * @tparam F A function type that should be convertible to a
     *   `LPTHREAD_START_ROUTINE` function pointer.
     */
    template <typename F>
    Win32Thread(F entry_point, void* parameter)
        : handle(CreateThread(nullptr, 0, entry_point, parameter, 0, nullptr),
                 CloseHandle) {}

   private:
    /**
     * The handle for the thread that is running, will be a null pointer if this
     * class was constructed with the default constructor.
     */
    std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(&CloseHandle)>
        handle;
};

/**
 * A simple RAII wrapper around `SetTimer`. Does not support timer procs since
 * we don't use them.
 */
class Win32Timer {
   public:
    Win32Timer(HWND window_handle, size_t timer_id, unsigned int interval_ms);
    ~Win32Timer();

    Win32Timer(const Win32Timer&) = delete;
    Win32Timer& operator=(const Win32Timer&) = delete;

    Win32Timer(Win32Timer&&);
    Win32Timer& operator=(Win32Timer&&);

   private:
    HWND window_handle;
    std::optional<size_t> timer_id;
};
