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

#include "../boost-fix.h"

#define NOMINMAX
#define NOSERVICE
#define NOMCX
#define NOIMM
#define WIN32_LEAN_AND_MEAN
#include <vestige/aeffectx.h>
#include <windows.h>

#include <boost/asio/local/stream_protocol.hpp>
#include <mutex>

#include "../../common/communication/vst2.h"
#include "../../common/configuration.h"
#include "../../common/logging/common.h"
#include "../editor.h"
#include "../utils.h"

/**
 * This hosts a Windows VST2 plugin, forwards messages sent by the Linux VST
 * plugin and provides host callback function for the plugin to talk back.
 *
 * @remark Because of Win32 API limitations, all window handling has to be done
 *   from a single thread. Most plugins won't have any issues when using
 *   multiple message loops, but the Melda plugins for instance will only update
 *   their GUIs from the message loop of the thread that created the first
 *   instance. This is why we pass an IO context to this class so everything
 *   that's not performance critical (audio and midi event handling) is handled
 *   on the same thread, even when hosting multiple plugins.
 */
class Vst2Bridge {
   public:
    /**
     * Initializes the Windows VST plugin and set up communication with the
     * native Linux VST plugin.
     *
     * @param main_context The main IO context for this application. Most events
     *   will be dispatched to this context, and the event handling loop should
     *   also be run from this context.
     * @param plugin_dll_path A (Unix style) path to the VST plugin .dll file to
     *   load.
     * @param endpoint_base_dir The base directory used for the socket
     *   endpoints. See `Sockets` for more information.
     *
     * @note The object has to be constructed from the same thread that calls
     *   `main_context.run()`.
     *
     * @throw std::runtime_error Thrown when the VST plugin could not be loaded,
     *   or if communication could not be set up.
     */
    Vst2Bridge(MainContext& main_context,
               std::string plugin_dll_path,
               std::string endpoint_base_dir);

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
    void handle_dispatch();

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
     * `Vst2Bridge::editor` for more information.
     */
    void handle_win32_events();

    /**
     * Forward the host callback made by the plugin to the host and return the
     * results.
     */
    intptr_t host_callback(AEffect*, int, int, intptr_t, void*, float);

    /**
     * With the `audioMasterGetTime` host callback the plugin expects the return
     * value from the calblack to be a pointer to a VstTimeInfo struct. If the
     * host did not support a certain time info query, than we'll store the
     * returned null pointer as a nullopt.
     */
    std::optional<VstTimeInfo> time_info;

    /**
     * The path to the .dll being loaded in the Wine VST host.
     */
    const boost::filesystem::path vst_plugin_path;

   private:
    /**
     * A wrapper around `plugin->dispatcher` that handles the opening and
     * closing of GUIs. Used inside of `handle_dispatch()`.
     */
    intptr_t dispatch_wrapper(AEffect* plugin,
                              int opcode,
                              int index,
                              intptr_t value,
                              void* data,
                              float option);

    /**
     * The IO context used for event handling so that all events and window
     * message handling can be performed from a single thread, even when hosting
     * multiple plugins.
     */
    MainContext& main_context;

    /**
     * The configuration for this instance of yabridge based on the `.so` file
     * that got loaded by the host. This configuration gets loaded on the plugin
     * side, and then sent over to the Wine host as part of the startup process.
     */
    Configuration config;

    // FIXME: This emits `-Wignored-attributes` as of Wine 5.22
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"

    /**
     * The shared library handle of the VST plugin. I sadly could not get
     * Boost.DLL to work here, so we'll just load the VST plugisn by hand.
     */
    std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&FreeLibrary)>
        plugin_handle;

#pragma GCC diagnostic pop

    /**
     * The loaded plugin's `AEffect` struct, obtained using the above library
     * handle.
     */
    AEffect* plugin;

    /**
     * The thread that responds to `getParameter` and `setParameter` requests.
     */
    Win32Thread parameters_handler;
    /**
     * The thread that handles calls to `processReplacing` (and `process` as a
     * fallback) and `processDoubleReplacing`.
     */
    Win32Thread process_replacing_handler;

    /**
     * All sockets used for communicating with this specific plugin.
     *
     * NOTE: This is defined **after** the threads on purpose. This way the
     *       sockets will be closed first, and we can then safely wait for the
     *       threads to exit.
     */
    Vst2Sockets<Win32Thread> sockets;

    /**
     * The MIDI events that have been received **and processed** since the last
     * call to `processReplacing()`. 99% of plugins make a copy of the MIDI
     * events they receive but some plugins such as Kontakt only store pointers
     * to these events, which means that the actual `VstEvent` objects must live
     * at least until the next audio buffer gets processed.
     */
    std::vector<DynamicVstEvents> next_audio_buffer_midi_events;
    /**
     * Mutex for locking the above event queue, since recieving and processing
     * now happens in two different threads.
     */
    std::mutex next_buffer_midi_events_mutex;

    /**
     * The plugin editor window. Allows embedding the plugin's editor into a
     * Wine window, and embedding that Wine window into a window provided by the
     * host. Should be empty when the editor is not open.
     *
     * @see should_postpone_message_loop
     */
    std::optional<Editor> editor;
};
