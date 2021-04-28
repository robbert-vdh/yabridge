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

#pragma once

#include "../boost-fix.h"

#ifndef NOMINMAX
#define NOMINMAX
#define WINE_NOWINSOCK
#endif
#include <vestige/aeffectx.h>
#include <windows.h>

#include "../../common/communication/vst2.h"
#include "../../common/configuration.h"
#include "../editor.h"
#include "../utils.h"
#include "common.h"

/**
 * This hosts a Windows VST2 plugin, forwards messages sent by the Linux VST
 * plugin and provides host callback function for the plugin to talk back.
 */
class Vst2Bridge : public HostBridge {
   public:
    /**
     * Initializes the Windows VST2 plugin and set up communication with the
     * native Linux VST2 plugin.
     *
     * @param main_context The main IO context for this application. Most events
     *   will be dispatched to this context, and the event handling loop should
     *   also be run from this context.
     * @param plugin_dll_path A (Unix style) path to the VST2 plugin .dll file
     *   to load.
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

    bool inhibits_event_loop() override;

    /**
     * Here we'll handle incoming `dispatch()` messages until the sockets get
     * closed during `effClose()`.
     */
    void run() override;

    void handle_x11_events() override;

    /**
     * Forward the host callback made by the plugin to the host and return the
     * results.
     */
    intptr_t host_callback(AEffect*, int, int, intptr_t, void*, float);

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
     * A logger instance we'll use log cached `audioMasterGetTime()` calls, so
     * they can be hidden on verbosity levels below 2.
     *
     * This only has to be used instead of directly writing to `std::cerr` when
     * the message should be hidden on lower verbosity levels.
     */
    Vst2Logger logger;

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

    /**
     * We'll store the last transport information obtained from the host as a
     * result of `audioMasterGetTime()` here so we can return a pointer to it if
     * the request was successful. To prevent unnecessary back and forth
     * communication, we'll send a copy of the current transport information to
     * the plugin as part of the audio processing call.
     *
     * @see cached_time_info
     */
    VstTimeInfo last_time_info;

    /**
     * This will temporarily cache the current time info during an audio
     * processing call to avoid an additional callback every processing cycle.
     * Some faulty plugins may even request this information for every sample,
     * which would otherwise cause a very noticeable performance hit.
     */
    ScopedValueCache<VstTimeInfo> time_info_cache;

    /**
     * Some plugins will also ask for the current process level during audio
     * processing, so we'll also cache that to prevent expensive callbacks.
     */
    ScopedValueCache<int> process_level_cache;

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
     * Whether `effOpen()` has already been called. Used in
     * `HostBridge::inhibits_event_loop` to work around a bug in T-RackS 5.
     */
    bool is_initialized = false;

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
     * The plugin editor window. Allows embedding the plugin's editor into a
     * Wine window, and embedding that Wine window into a window provided by the
     * host. Should be empty when the editor is not open.
     */
    std::optional<Editor> editor;

    /**
     * The MIDI events that have been received **and processed** since the last
     * call to `processReplacing()`. 99% of plugins make a copy of the MIDI
     * events they receive but some plugins such as Kontakt only store pointers
     * to these events, which means that the actual `VstEvent` objects must live
     * at least until the next audio buffer gets processed.
     */
    std::vector<DynamicVstEvents> next_audio_buffer_midi_events;
    /**
     * Whether `next_audio_buffer_midi_events` should be cleared before
     * inserting new events.
     *
     * HACK: Normally we should be able to clear these immediately after the
     *       processing call, but Native Instruments' FM7 requires the last MIDI
     *       event to stay alive if there have not been any new MIDI events
     *       during the current processing cycle.
     */
    bool should_clear_midi_events = false;
    /**
     * Mutex for locking the above event queue, since recieving and processing
     * now happens in two different threads.
     */
    std::mutex next_buffer_midi_events_mutex;
};
