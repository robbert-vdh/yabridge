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

#include "../editor.h"

/**
 * The base for the Wine plugin host bridge interface for all plugin types. This
 * only has to be able to handle Win32 and X11 events. Implementations of this
 * will actually host a plugin and do all the function call forwarding.
 */
class HostBridge {
   public:
    virtual ~HostBridge(){};

    /**
     * Handle events until the plugin exits. The actual events are posted to
     * `main_context` to ensure that all operations to could potentially
     * interact with Win32 code are run from a single thread, even when hosting
     * multiple plugins. The message loop should be run on a timer within the
     * same IO context.
     *
     * @note Because of the reasons mentioned above, for this to work the plugin
     *   should be initialized within the same thread that calls
     *   `main_context.run()`.
     */
    virtual void run() = 0;

    /**
     * Handle X11 events for the editor window if it is open. This can safely be
     * run from any thread.
     */
    void handle_x11_events();

    /**
     * Run the message loop for this plugin. This is only used for the
     * individual plugin host, so that we can filter out some unnecessary timer
     * events. When hosting multiple plugins, a simple central message loop
     * should be used instead. This is run on a timer in the same IO context as
     * the one that handles the events, i.e. `main_context`.
     *
     * Because of the way the Win32 API works we have to process events on the
     * same thread as the one the window was created on, and that thread is the
     * thread that's handling dispatcher calls. Some plugins will also rely on
     * the Win32 message loop to run tasks on a timer and to defer loading, so
     * we have to make sure to always run this loop. The only exception is a in
     * specific situation that can cause a race condition in some plugins
     * because of incorrect assumptions made by the plugin. See the dostring for
     * `HostBridge::editor` for more information.
     */
    void handle_win32_events();

   protected:
    /**
     * The plugin editor window. Allows embedding the plugin's editor into a
     * Wine window, and embedding that Wine window into a window provided by the
     * host. Should be empty when the editor is not open.
     *
     * @see should_postpone_message_loop
     */
    std::optional<Editor> editor;
};
