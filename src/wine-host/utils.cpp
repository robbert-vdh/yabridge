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

#include "utils.h"

PluginContext::PluginContext() : context(), events_timer(context) {}

uint32_t WINAPI win32_thread_trampoline(std::function<void()>* entry_point) {
    (*entry_point)();
    delete entry_point;

    return 0;
}

Win32Thread::~Win32Thread() {
    // Imitate std::jthread's joining behaviour, the thread handle will be
    // cleaned up by the `std::unique_ptr`
    if (handle) {
        WaitForSingleObject(handle.get(), INFINITE);
    }
}

Win32Thread::Win32Thread(Win32Thread&& o) : handle(std::move(o.handle)) {}

Win32Thread& Win32Thread::operator=(Win32Thread&& o) {
    handle = std::move(o.handle);

    return *this;
}

void PluginContext::run() {
    context.run();
}

void PluginContext::stop() {
    context.stop();
}

Win32Thread::Win32Thread() : handle(nullptr, nullptr) {}

Win32Timer::Win32Timer(HWND window_handle,
                       size_t timer_id,
                       unsigned int interval_ms)
    : window_handle(window_handle), timer_id(timer_id) {
    SetTimer(window_handle, timer_id, interval_ms, nullptr);
}

Win32Timer::~Win32Timer() {
    if (timer_id) {
        KillTimer(window_handle, *timer_id);
    }
}

Win32Timer::Win32Timer(Win32Timer&& o) : timer_id(o.timer_id) {
    o.timer_id = std::nullopt;
}

Win32Timer& Win32Timer::operator=(Win32Timer&& o) {
    timer_id = o.timer_id;
    o.timer_id = std::nullopt;

    return *this;
}
