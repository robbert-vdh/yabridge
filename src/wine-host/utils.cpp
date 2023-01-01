// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2023 Robbert van der Helm
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

#include <iostream>

#include "bridges/common.h"

using namespace std::literals::chrono_literals;

uint32_t WINAPI
win32_thread_trampoline(fu2::unique_function<void()>* entry_point) {
    (*entry_point)();
    delete entry_point;

    return 0;
}

Win32Thread::Win32Thread() noexcept : handle_(nullptr, nullptr) {}

Win32Thread::~Win32Thread() noexcept {
    if (handle_) {
        WaitForSingleObject(handle_.get(), INFINITE);
    }
}

Win32Thread::Win32Thread(Win32Thread&& o) noexcept
    : handle_(std::move(o.handle_)) {
    o.handle_.reset();
}

Win32Thread& Win32Thread::operator=(Win32Thread&& o) noexcept {
    handle_ = std::move(o.handle_);
    o.handle_.reset();

    return *this;
}

Win32Timer::Win32Timer() noexcept {}

Win32Timer::Win32Timer(HWND window_handle,
                       size_t timer_id,
                       unsigned int interval_ms) noexcept
    : window_handle_(window_handle), timer_id_(timer_id) {
    SetTimer(window_handle, timer_id, interval_ms, nullptr);
}

Win32Timer::~Win32Timer() noexcept {
    if (timer_id_) {
        KillTimer(window_handle_, *timer_id_);
    }
}

Win32Timer::Win32Timer(Win32Timer&& o) noexcept
    : window_handle_(o.window_handle_), timer_id_(std::move(o.timer_id_)) {
    o.timer_id_.reset();
}

Win32Timer& Win32Timer::operator=(Win32Timer&& o) noexcept {
    window_handle_ = o.window_handle_;
    timer_id_ = std::move(o.timer_id_);
    o.timer_id_.reset();

    return *this;
}

MainContext::MainContext()
    : context_(),
      events_timer_(context_),
      watchdog_context_(),
      watchdog_timer_(watchdog_context_) {}

void MainContext::run() {
    // We need to know which thread is the GUI thread because mutual recursion
    // in VST3 plugins needs to be handled differently depending on whether the
    // potentially mutually recursive function was called from an audio thread
    // or a GUI thread
    gui_thread_id_ = GetCurrentThreadId();

    // NOTE: We allow disabling the watchdog timer to allow the Wine process to
    //       be run from a separate namespace. This is not something you'd
    //       normally want to enable.
    if (is_watchdog_timer_disabled()) {
        std::cerr << "WARNING: Watchdog timer disabled. Not protecting"
                  << std::endl;
        std::cerr << "         against dangling processes." << std::endl;
    } else {
        // To account for hosts terminating before the bridged plugin has
        // initialized, we'll do the first watchdog check five seconds. After
        // this we'll run the timer on a 30 second interval.
        async_handle_watchdog_timer(5s);

        watchdog_handler_ = Win32Thread([&]() {
            pthread_setname_np(pthread_self(), "watchdog");

            watchdog_context_.run();
        });
    }

    context_.run();

    // We only need to check if the host is still running while the main context
    // is also running. If a stop was requested, the entire application is
    // supposed to shut down. Otherwise `watchdog_handler` would just block on
    // the join as the watchdog timer is still active.
    watchdog_context_.stop();
}

void MainContext::stop() noexcept {
    context_.stop();
}

void MainContext::update_timer_interval(
    std::chrono::steady_clock::duration new_interval) noexcept {
    timer_interval_ = new_interval;
}

MainContext::WatchdogGuard::WatchdogGuard(
    HostBridge& bridge,
    std::unordered_set<HostBridge*>& watched_bridges,
    std::mutex& watched_bridges_mutex)
    : bridge_(&bridge),
      watched_bridges_(watched_bridges),
      watched_bridges_mutex_(watched_bridges_mutex) {
    std::lock_guard lock(watched_bridges_mutex);
    watched_bridges.insert(&bridge);
}

MainContext::WatchdogGuard::~WatchdogGuard() noexcept {
    if (is_active_) {
        std::lock_guard lock(watched_bridges_mutex_.get());
        watched_bridges_.get().erase(bridge_);
    }
}

MainContext::WatchdogGuard::WatchdogGuard(WatchdogGuard&& o) noexcept
    : bridge_(std::move(o.bridge_)),
      watched_bridges_(std::move(o.watched_bridges_)),
      watched_bridges_mutex_(std::move(o.watched_bridges_mutex_)) {
    o.is_active_ = false;
}

MainContext::WatchdogGuard& MainContext::WatchdogGuard::operator=(
    WatchdogGuard&& o) noexcept {
    bridge_ = std::move(o.bridge_);
    watched_bridges_ = std::move(o.watched_bridges_);
    watched_bridges_mutex_ = std::move(o.watched_bridges_mutex_);
    o.is_active_ = false;

    return *this;
}

MainContext::WatchdogGuard MainContext::register_watchdog(HostBridge& bridge) {
    // The guard's constructor and destructor will handle actually registering
    // and unregistering the bridge from `watched_bridges`
    return WatchdogGuard(bridge, watched_bridges_, watched_bridges_mutex_);
}

void MainContext::async_handle_watchdog_timer(
    std::chrono::steady_clock::duration interval) {
    watchdog_timer_.expires_at(std::chrono::steady_clock::now() + interval);
    watchdog_timer_.async_wait([&](const std::error_code& error) {
        if (error) {
            return;
        }

        // When the `WatchdogGuard` field on `HostBridge` gets destroyed, that
        // bridge instance will be removed from `watched_bridges`. So if our
        // call to `HostBridge::shutdown_if_dangling()` shuts the plugin down,
        // the instance will be removed after this lambda exits.
        std::lock_guard lock(watched_bridges_mutex_);
        for (auto& bridge : watched_bridges_) {
            bridge->shutdown_if_dangling();
        }

        async_handle_watchdog_timer(30s);
    });
}
