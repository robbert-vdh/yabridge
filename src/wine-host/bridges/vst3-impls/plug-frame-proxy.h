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

#include "../vst3.h"

class Vst3PlugFrameProxyImpl : public Vst3PlugFrameProxy {
   public:
    Vst3PlugFrameProxyImpl(Vst3Bridge& bridge,
                           Vst3PlugFrameProxy::ConstructArgs&& args);

    /**
     * We'll override the query interface to log queries for interfaces we do
     * not (yet) support.
     */
    tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                      void** obj) override;

    /**
     * This is needed to be able to handle a call to `IPlugView::onSize()` from
     * the UI thread while the plugin is currently calling
     * `IPlugFrame::resizeView()` from that same thread.. This is probably the
     * hackiest (and most error prone, probably) part of this VST3
     * implementation. The details on how this works and why it is necessary are
     * explained in the comment in `YaPlugFrameProxyImpl::resizeView()`.
     *
     * If there is currently a call to `resizeView()` being processed, then this
     * will run `on_size` from the same thread that's currently processing a
     * call to `resizeView()` and return the result from the function call.
     * Otherwise this will return a nullopt and `on_size` should be passed to
     * `main_context.run_in_context<tresult>()`.
     */
    template <typename F>
    std::optional<tresult> maybe_run_on_size_from_ui_thread(F on_size) {
        {
            std::lock_guard lock(on_size_interrupt_mutex);
            if (!on_size_interrupt_waiting) {
                return std::nullopt;
            }

            on_size_interrupt = std::move(on_size);
        }
        on_size_interrupt_cv.notify_one();

        // Since `on_size` is run from another thread, we now have to wait to be
        // woken up again when the result is ready
        std::unique_lock lock(on_size_interrupt_mutex);
        on_size_interrupt_cv.wait(
            lock, [&]() { return on_size_interrupt_result.has_value(); });

        return *on_size_interrupt_result;
    }

    // From `IPlugFrame`
    tresult PLUGIN_API resizeView(Steinberg::IPlugView* view,
                                  Steinberg::ViewRect* newSize) override;

   private:
    Vst3Bridge& bridge;

    /**
     * A function that will be used to run `IPlugView::onSize()` on the thread
     * that originally called `IPlugFrame::resizeView()` to work around a Win32
     * limitation, along with its result, and whether or not we're currently
     * waiting for this function to be provided by some other thread. See the
     * comment in `onSize()` for more information.
     */
    bool on_size_interrupt_waiting = false;
    std::optional<fu2::unique_function<tresult()>> on_size_interrupt;
    std::optional<tresult> on_size_interrupt_result;
    std::condition_variable on_size_interrupt_cv;
    std::mutex on_size_interrupt_mutex;
};
