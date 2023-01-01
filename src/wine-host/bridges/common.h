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

#pragma once

#include "../use-linux-asio.h"

#include <ghc/filesystem.hpp>

#include "../../common/logging/common.h"
#include "../utils.h"

/**
 * The base for the Wine plugin host bridge interfaces for all plugin types.
 * This mostly concerns event handling, and some common setup like loggers and a
 * watchdog timer to let us shut down the sockets when the native host has
 * exited while the sockets are still alive. Implementations of this will
 * actually host a plugin and do all the function call forwarding.
 */
class HostBridge {
   protected:
    HostBridge(MainContext& main_context,
               ghc::filesystem::path plugin_path,
               pid_t parent_pid);

   public:
    virtual ~HostBridge() noexcept = default;

    /**
     * If a plugin instance returns `true` here, then the event loop should not
     * be run. Some very specific plugins, like the T-RackS 5 plugins, will set
     * up a Win32 timer in their constructor, but since the plugins are left in
     * a partially initialized state until `effOpen()` has been called running
     * the Win32 message loop before that time will trigger a race condition
     * within those plugins. This is very much an issue with those particular
     * plugins, but since this situation wouldn't occur on Windows we'll just
     * have to work around it.
     *
     * @relates MainContext::async_handle_events
     */
    virtual bool inhibits_event_loop() noexcept = 0;

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
     * Run the message loop for this plugin. This should be called from a timer.
     * X11 events for the open editors are also handled in this same way,
     * because they are run from a Win32 timer. This lets us still process those
     * events even when the Win32 event loop blocks the GUI thread. Since this
     * function doesn't have any per-plugin behavior, only a single invocation
     * of this is needed when hosting multiple plugins. This is run on a timer
     * in the same IO context as the one that handles the events, i.e.
     * `main_context_`.
     *
     * Because of the way the Win32 API works we have to process events on the
     * same thread as the one the window was created on, and that thread is the
     * thread that's handling dispatcher calls. Some plugins will also rely on
     * the Win32 message loop to run tasks on a timer and to defer loading, so
     * we have to make sure to always run this loop. The only exception is a in
     * specific situation that can cause a race condition in some plugins
     * because of incorrect assumptions made by the plugin. See the dostring for
     * `Vst2Bridge::editor` for more information.
     */
    static void handle_events() noexcept;

    /**
     * Used as part of the watchdog. This will check whether the remote host
     * process this bridge is connected with is still active. If it is not, then
     * we'll close the sockets, which will cause this process to exit
     * gracefully.
     */
    void shutdown_if_dangling();

    /**
     * The path to the .dll being loaded in the Wine plugin host.
     */
    const ghc::filesystem::path plugin_path_;

    /**
     * The IO context used for event handling so that all events and window
     * message handling can be performed from a single thread, even when hosting
     * multiple plugins.
     */
    MainContext& main_context_;

   protected:
    /**
     * Used as part of the watchdog that shuts down a plugin when the remote
     * native host process dies. This is used to prevent plugins from hanging
     * indefinitely on a `recv()`. This function should just call
     * `sockets.close()`.
     */
    virtual void close_sockets() = 0;

    /**
     * A logger, just like we have on the plugin side. This is normally not
     * needed because we can just print to STDERR, but this way we can
     * conditionally hide output based on the verbosity level.
     *
     * @see Logger::create_wine_stderr
     */
    Logger generic_logger_;

   private:
    /**
     * The process ID of the native plugin host we are bridging for. This should
     * be the parent, but it might not be because of Wine's startup script,
     * `WINELOADER`s and Wine's `start.exe` behaviour. We'll periodically check
     * if this process is still alive, and close the sockets if it is not to
     * prevent dangling processes.
     */
    const pid_t parent_pid_;

    /**
     * A guard that, while in scope, will cause `shutdown_if_dangling()` to
     * periodically be called.
     */
    MainContext::WatchdogGuard watchdog_guard_;
};
