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

#include <memory>
#include <optional>

#define NOMINMAX
#define NOSERVICE
#define NOMCX
#define NOIMM
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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
