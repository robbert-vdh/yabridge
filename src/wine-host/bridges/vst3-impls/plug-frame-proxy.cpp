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

#include "plug-frame-proxy.h"

#include <iostream>

Vst3PlugFrameProxyImpl::Vst3PlugFrameProxyImpl(
    Vst3Bridge& bridge,
    Vst3PlugFrameProxy::ConstructArgs&& args)
    : Vst3PlugFrameProxy(std::move(args)), bridge(bridge) {
    // The lifecycle is thos object is managed together with that of the plugin
    // object instance instance this belongs to
}

tresult PLUGIN_API
Vst3PlugFrameProxyImpl::queryInterface(const Steinberg::TUID _iid, void** obj) {
    // TODO: Successful queries should also be logged
    const tresult result = Vst3PlugFrameProxy::queryInterface(_iid, obj);
    if (result != Steinberg::kResultOk) {
        std::cerr << "TODO: Implement unknown interface logging on Wine side "
                     "for Vst3PlugFrameProxyImpl::queryInterface"
                  << std::endl;
    }

    return result;
}

tresult PLUGIN_API
Vst3PlugFrameProxyImpl::resizeView(Steinberg::IPlugView* /*view*/,
                                   Steinberg::ViewRect* newSize) {
    if (newSize) {
        // XXX: Since VST3 currently only support a single view type we'll
        //      assume `view` is the `IPlugView*` returned by the last call to
        //      `IEditController::createView()`

        // HACK: This ia bit of a weird one and requires special handling. A
        //       plugin will call this function from the Win32 message loop so
        //       the call blocks the loop. The host will then check with the
        //       plugin if it can actually resize itself to `*newSize`, and it
        //       will then call `IPlugView::onSize()` with the new size. The
        //       issue is that the `IPlugView::onSize()` call also has to be
        //       called from within the UI thread, but that thread is currently
        //       being blocked by the call to this function.
        //       As a workaround, we'll send the message for the call to
        //       `IPlugFrame::resizeView()` on another thread. We then wait for
        //       either that request to finish immediately (meaning the host
        //       hasn't resized the window), or for `IPlugView::onSize()` to be
        //       called by the host. If the host does call `IPlugView::onSize()`
        //       while the other thread is handling `IPlugFrame::resizeView()`,
        //       then we'll awaken this thread using a condition variable so we
        //       can do the actual call to `IPlugView::onSize()` from here, from
        //       within the Win32 loop.
        // TODO: Can we someone use Boost.Asio strands to make this cleaner?
        {
            std::lock_guard lock(on_size_interrupt_mutex);
            on_size_interrupt_waiting = true;
            on_size_interrupt.reset();
            on_size_interrupt_result.reset();
        }

        std::promise<tresult> resize_result_promise{};
        std::future<tresult> resize_result_future =
            resize_result_promise.get_future();
        Win32Thread resize_thread([&]() {
            const tresult result = bridge.send_message(YaPlugFrame::ResizeView{
                .owner_instance_id = owner_instance_id(),
                .new_size = *newSize});

            resize_result_promise.set_value(result);

            {
                std::lock_guard lock(on_size_interrupt_mutex);
                if (BOOST_LIKELY(!on_size_interrupt_waiting)) {
                    return;
                }

                // If the call to `IPlugFrame::resizeView()` finish without the
                // host calling `IPlugView::onSize()` (e.g. when the call
                // failed), then we'll have to manually unblock the thread below
                // by providing a noop function.
                on_size_interrupt = []() { return Steinberg::kResultOk; };
            }
            on_size_interrupt_cv.notify_one();

            // We don't need this result value, but we should still wait for
            // it
            std::unique_lock lock(on_size_interrupt_mutex);
            on_size_interrupt_cv.wait(
                lock, [&]() { return on_size_interrupt_result.has_value(); });
        });

        // Wait for `IPlugView::onSize()` to be called by the host and handle
        // the call here on this thread, or wait for the call to
        // `IPlugFrame::resizeView()` in the above thread to finish. In the last
        // case we'll execute a noop function.
        std::unique_lock lock(on_size_interrupt_mutex);
        on_size_interrupt_cv.wait(
            lock, [&]() { return on_size_interrupt.has_value(); });

        // When we get a function through one of the two above means, we'll
        // store the result and then awake the calling thread again so it can
        // access the result
        on_size_interrupt_result = (*on_size_interrupt)();
        on_size_interrupt_waiting = false;

        lock.unlock();
        on_size_interrupt_cv.notify_one();

        return resize_result_future.get();
    } else {
        std::cerr
            << "WARNING: Null pointer passed to 'IPlugFrame::resizeView()'"
            << std::endl;
        return Steinberg::kInvalidArgument;
    }
}
