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

#include "../../common/configuration.h"
#include "../../common/logging.h"
#include "../editor.h"
#include "../utils.h"

/**
 * A marker struct to indicate that the editor is about to be opened.
 *
 * @see Vst2Bridge::editor
 */
struct EditorOpening {};

/**
 * This hosts a Windows VST2 plugin, forwards messages sent by the Linux VST
 * plugin and provides host callback function for the plugin to talk back.
 *
 * @remark Because of Win32 API limitations, all window handling has to be done
 *   from the same thread. Most plugins won't have any issues when using
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
     * @param socket_endpoint_path A (Unix style) path to the Unix socket
     *   endpoint the native VST plugin created to communicate over.
     *
     * @note The object has to be constructed from the same thread that calls
     *   `main_context.run()`.
     *
     * @throw std::runtime_error Thrown when the VST plugin could not be loaded,
     *   or if communication could not be set up.
     */
    Vst2Bridge(boost::asio::io_context& main_context,
               std::string plugin_dll_path,
               std::string socket_endpoint_path);

    /**
     * Returns true if the message loop should be skipped. This happens when the
     * editor is in the process of being opened. In VST hosts on Windows
     * `effEditOpen()` and `effEditGetRect()` will always be called in sequence,
     * but in our approach there will be an opportunity to handle events in
     * between these two calls. Most plugins will handle this just fine, but
     * some plugins end up blocking indefinitely while waiting for the other
     * function to be called, hence why this function is needed. For
     * individually hosted plugins this check is done implicitely in
     * `Vst2Bridge::handle_win32_events()`.
     */
    bool should_skip_message_loop() const;

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
     * Handle X11 events for the editor window if it is open. This can be run
     * safely from any thread.
     */
    void handle_x11_events();

    /**
     * Run the message loop for this plugin. This is only used for the
     * individual plugin host. When hosting multiple plugins, a simple central
     * message loop with a check to `should_skip_message_loop()` should be used
     * instead. This is run on a timer in the same IO context as the one that
     * handles the events, i.e. `main_context`.
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

    // These functions are the entry points for the `*_handler` threads
    // defined below. They're defined here because we can't use lambdas with
    // WinAPI's `CreateThread` which is needed to support the proper call
    // conventions the VST plugins expect.
    void handle_dispatch_midi_events();
    void handle_parameters();
    void handle_process_replacing();

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
    boost::asio::io_context& io_context;

    /**
     * The configuration for this instance of yabridge based on the `.so` file
     * that got loaded by the host. This configuration gets loaded on the plugin
     * side, and then sent over to the Wine host as part of the startup process.
     */
    Configuration config;

    /**
     * The shared library handle of the VST plugin. I sadly could not get
     * Boost.DLL to work here, so we'll just load the VST plugisn by hand.
     */
    std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&FreeLibrary)>
        plugin_handle;

    /**
     * The loaded plugin's `AEffect` struct, obtained using the above library
     * handle.
     */
    AEffect* plugin;

    /**
     * The UNIX domain socket endpoint used for communicating to this specific
     * bridged plugin.
     */
    boost::asio::local::stream_protocol::endpoint socket_endpoint;

    // The naming convention for these sockets is `<from>_<to>_<event>`. For
    // instance the socket named `host_vst_dispatch` forwards
    // `AEffect.dispatch()` calls from the native VST host to the Windows VST
    // plugin (through the Wine VST host).

    /**
     * The socket that forwards all `dispatcher()` calls from the VST host to
     * the plugin.
     */
    boost::asio::local::stream_protocol::socket host_vst_dispatch;
    /**
     * Used specifically for the `effProcessEvents` opcode. This is needed
     * because the Win32 API is designed to block during certain GUI
     * interactions such as resizing a window or opening a dropdown. Without
     * this MIDI input would just stop working at times.
     */
    boost::asio::local::stream_protocol::socket host_vst_dispatch_midi_events;
    /**
     * The socket that forwards all `audioMaster()` calls from the Windows VST
     * plugin to the host.
     */
    boost::asio::local::stream_protocol::socket vst_host_callback;
    /**
     * Used for both `getParameter` and `setParameter` since they mostly
     * overlap.
     */
    boost::asio::local::stream_protocol::socket host_vst_parameters;
    boost::asio::local::stream_protocol::socket host_vst_process_replacing;

    /**
     * A control socket that sends data that is not suitable for the other
     * sockets. At the moment this is only used to, on startup, send the Windows
     * VST plugin's `AEffect` object to the native VST plugin, and to then send
     * the configuration (from `config`) back to the Wine host.
     */
    boost::asio::local::stream_protocol::socket host_vst_control;

    /**
     * The thread that specifically handles `effProcessEvents` opcodes so the
     * plugin can still receive MIDI during GUI interaction to work around Win32
     * API limitations.
     */
    Win32Thread dispatch_midi_events_handler;
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
     * A binary semaphore to prevent race conditions from the host callback
     * function being called by two threads at once. See `send_event()` for more
     * information.
     */
    std::mutex host_callback_mutex;

    /**
     * A scratch buffer for sending and receiving data during `process` and
     * `processReplacing` calls.
     */
    std::vector<uint8_t> process_buffer;

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
     * This field can have three possible states:
     *
     * - `std::nullopt` when the editor is closed.
     * - An `Editor` object when the editor is open.
     * - `EditorOpening` when the editor is not yet open, but the host has
     *   already called `effEditGetRect()` and is about to call `effEditOpen()`.
     *   This is needed because there is a race condition in some bugs that
     *   cause them to crash or enter an infinite Win32 message loop when
     *   `effEditGetRect()` gets dispatched and we then enter the message loop
     *   loop before `effEditOpen()` gets called. Most plugins will handle this
     *   just fine, but a select few plugins make the assumption that the editor
     *   is already open once `effEditGetRect()` has been called, even if
     *   `effEditOpen` has not yet been dispatched. VST hsots on Windows will
     *   call these two events in sequence, so the bug would never occur there.
     *   To work around this we'll use this third state to temporarily stop
     *   processing Windows events in the one or two ticks between these two
     *   events.
     *
     * @see should_postpone_message_loop
     */
    std::variant<std::monostate, Editor, EditorOpening> editor;
};
