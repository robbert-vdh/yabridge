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

#include "utils.h"

#include "bridges/common.h"

using namespace std::literals::chrono_literals;

uint32_t WINAPI
win32_thread_trampoline(fu2::unique_function<void()>* entry_point) {
    (*entry_point)();
    delete entry_point;

    return 0;
}

Win32Thread::Win32Thread(Win32Thread&& o) : handle(std::move(o.handle)) {
    o.handle.reset();
}

Win32Thread& Win32Thread::operator=(Win32Thread&& o) {
    handle = std::move(o.handle);
    o.handle.reset();

    return *this;
}

Win32Thread::~Win32Thread() {
    if (handle) {
        WaitForSingleObject(handle.get(), INFINITE);
    }
}

Win32Thread::Win32Thread() : handle(nullptr, nullptr) {}

Win32Timer::Win32Timer() {}

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

Win32Timer::Win32Timer(Win32Timer&& o)
    : window_handle(o.window_handle), timer_id(std::move(o.timer_id)) {
    o.timer_id.reset();
}

Win32Timer& Win32Timer::operator=(Win32Timer&& o) {
    window_handle = o.window_handle;
    timer_id = std::move(o.timer_id);
    o.timer_id.reset();

    return *this;
}

MainContext::MainContext()
    : context(),
      events_timer(context),
      watchdog_context(),
      watchdog_timer(watchdog_context) {
    async_handle_watchdog();

    watchdog_handler = Win32Thread([&]() {
        set_realtime_priority(false);
        watchdog_context.run();
    });
}

void MainContext::run() {
    context.run();
}

void MainContext::stop() {
    context.stop();
}

void MainContext::update_timer_interval(
    std::chrono::steady_clock::duration new_interval) {
    timer_interval = new_interval;
}

MainContext::WatchdogGuard::WatchdogGuard(
    HostBridge& bridge,
    std::set<HostBridge*>& watched_bridges,
    std::mutex& watched_bridges_mutex)
    : bridge(&bridge),
      watched_bridges(watched_bridges),
      watched_bridges_mutex(watched_bridges_mutex) {
    std::lock_guard lock(watched_bridges_mutex);
    watched_bridges.insert(&bridge);
}

MainContext::WatchdogGuard::~WatchdogGuard() {
    if (is_active) {
        std::lock_guard lock(watched_bridges_mutex.get());
        watched_bridges.get().erase(bridge);
    }
}

MainContext::WatchdogGuard::WatchdogGuard(WatchdogGuard&& o)
    : bridge(std::move(o.bridge)),
      watched_bridges(std::move(o.watched_bridges)),
      watched_bridges_mutex(std::move(o.watched_bridges_mutex)) {
    o.is_active = false;
}

MainContext::WatchdogGuard& MainContext::WatchdogGuard::operator=(
    WatchdogGuard&& o) {
    bridge = std::move(o.bridge);
    watched_bridges = std::move(o.watched_bridges);
    watched_bridges_mutex = std::move(o.watched_bridges_mutex);
    o.is_active = false;

    return *this;
}

MainContext::WatchdogGuard MainContext::register_watchdog(HostBridge& bridge) {
    // The guard's constructor and destructor will handle actually registering
    // and unregistering the bridge from `watched_bridges`
    return WatchdogGuard(bridge, watched_bridges, watched_bridges_mutex);
}

void MainContext::async_handle_watchdog() {
    // Try to keep a steady framerate, but add in delays to let other events
    // get handled if the GUI message handling somehow takes very long.
    watchdog_timer.expires_at(std::chrono::steady_clock::now() + 20s);
    watchdog_timer.async_wait([&](const boost::system::error_code& error) {
        if (error.failed()) {
            return;
        }

        // When the `WatchdogGuard` field on `HostBridge` gets destroyed, that
        // bridge instance will be removed from `watched_bridges`. So if our
        // call to `HostBridge::shutdown_if_dangling()` shuts the plugin down,
        // the instance will be removed after this lambda exits.
        std::lock_guard lock(watched_bridges_mutex);
        for (auto& bridge : watched_bridges) {
            bridge->shutdown_if_dangling();
        }

        async_handle_watchdog();
    });
}
