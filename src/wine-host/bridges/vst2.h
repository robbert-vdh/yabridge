// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2022 Robbert van der Helm
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

#include "../asio-fix.h"

#include <vestige/aeffectx.h>
#include <windows.h>

#include "../../common/communication/vst2.h"
#include "../../common/configuration.h"
#include "../../common/mutual-recursion.h"
#include "../editor.h"
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
     * @param parent_pid The process ID of the native plugin host this bridge is
     *   supposed to communicate with. Used as part of our watchdog to prevent
     *   dangling Wine processes.
     *
     * @note The object has to be constructed from the same thread that calls
     *   `main_context_.run()`.
     *
     * @throw std::runtime_error Thrown when the VST plugin could not be loaded,
     *   or if communication could not be set up.
     */
    Vst2Bridge(MainContext& main_context,
               std::string plugin_dll_path,
               std::string endpoint_base_dir,
               pid_t parent_pid);

    bool inhibits_event_loop() noexcept override;

    /**
     * Here we'll handle incoming `dispatch()` messages until the sockets get
     * closed during `effClose()`.
     */
    void run() override;

   protected:
    void close_sockets() override;

   public:
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
     * Sets up the shared memory audio buffers for this plugin instance and
     * returns the configuration so the native plugin can connect to it as well.
     * This should be called after `effMainsChanged()`.
     */
    AudioShmBuffer::Config setup_shared_audio_buffers();

    /**
     * A logger instance we'll use log cached `audioMasterGetTime()` calls, so
     * they can be hidden on verbosity levels below 2.
     *
     * This only has to be used instead of directly writing to `std::cerr` when
     * the message should be hidden on lower verbosity levels.
     */
    Vst2Logger logger_;

    /**
     * The configuration for this instance of yabridge based on the `.so` file
     * that got loaded by the host. This configuration gets loaded on the plugin
     * side, and then sent over to the Wine host as part of the startup process.
     */
    Configuration config_;

    /**
     * A shared memory object we'll write the input audio buffers to on the
     * native plugin side. We'll then let the plugin write its outputs here on
     * the Wine side. The buffer will be configured during `effMainsChanged`. At
     * that point we'll build the configuration for the object here, on the Wine
     * side, and then we'll initialize the buffers using that configuration.
     * This same configuration is then used on the native plugin side to connect
     * to this same shared memory object. We keep track of the maximum block
     * size and the processing precision indicated by the host so we know how
     * large this buffer needs to be in advance.
     */
    std::optional<AudioShmBuffer> process_buffers_;

    /**
     * Pointers to the input channels in process_buffers so we can pass them to
     * the plugin. These can be either `float*` or `double*`, so we sadly have
     * to use void pointers here.
     */
    std::vector<void*> process_buffers_input_pointers_;

    /**
     * Pointers to the output channels in process_buffers so we can pass them to
     * the plugin. These can be either `float*` or `double*`, so we sadly have
     * to use void pointers here.
     */
    std::vector<void*> process_buffers_output_pointers_;

    /**
     * The maximum number of samples the host will pass to the plugin during
     * `processReplacing()`/`processDoubleReplacing()`/`process()`. This is
     * indicated using a call to `effSetBlockSize()` prior to
     * `effMainsChanged()`.
     *
     * Some hosts forget to call this, so it will be a nullopt until it is
     * called. In that case we'll use the value obtained through
     * `audioMasterGetBlockSize()` instead.
     */
    std::optional<uint32_t> max_samples_per_block_;

    /**
     * Whether the host is going to send double precision audio or not. This
     * will only be the case if the host has called `effSetProcessPrecision()`
     * with `kVstProcessPrecision64` before the call to `effMainsChanged()`.
     */
    bool double_precision_ = false;

    /**
     * We'll store the last transport information obtained from the host as a
     * result of `audioMasterGetTime()` here so we can return a pointer to it if
     * the request was successful. To prevent unnecessary back and forth
     * communication, we'll prefetch the current transport information in the
     * plugin as part of the audio processing call.
     *
     * @see cached_time_info_
     */
    VstTimeInfo last_time_info_;

    /**
     * This will temporarily cache the current time info during an audio
     * processing call to avoid an additional callback every processing cycle.
     * Some faulty plugins may even request this information for every sample,
     * which would otherwise cause a very noticeable performance hit.
     */
    ScopedValueCache<VstTimeInfo> time_info_cache_;

    /**
     * Some plugins will also ask for the current process level during audio
     * processing, so we'll also prefetch that to prevent expensive callbacks.
     */
    ScopedValueCache<int> process_level_cache_;

    // FIXME: This emits `-Wignored-attributes` as of Wine 5.22
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"

    /**
     * The shared library handle of the VST2 plugin.
     */
    std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&FreeLibrary)>
        plugin_handle_;

#pragma GCC diagnostic pop

    /**
     * The loaded plugin's `AEffect` struct, obtained using the above library
     * handle.
     */
    AEffect* plugin_;

    /**
     * Whether `effOpen()` has already been called. Used in
     * `HostBridge::inhibits_event_loop` to work around a bug in T-RackS 5.
     */
    bool is_initialized_ = false;

    /**
     * The thread that responds to `getParameter` and `setParameter` requests.
     */
    Win32Thread parameters_handler_;
    /**
     * The thread that handles calls to `processReplacing` (and `process` as a
     * fallback) and `processDoubleReplacing`.
     */
    Win32Thread process_replacing_handler_;

    /**
     * All sockets used for communicating with this specific plugin.
     *
     * NOTE: This is defined **after** the threads on purpose. This way the
     *       sockets will be closed first, and we can then safely wait for the
     *       threads to exit.
     */
    Vst2Sockets<Win32Thread> sockets_;

    /**
     * The plugin editor window. Allows embedding the plugin's editor into a
     * Wine window, and embedding that Wine window into a window provided by the
     * host. Should be empty when the editor is not open.
     */
    std::optional<Editor> editor_;

    /**
     * The MIDI events that have been received **and processed** since the last
     * call to `processReplacing()`. 99% of plugins make a copy of the MIDI
     * events they receive but some plugins such as Kontakt only store pointers
     * to these events, which means that the actual `VstEvent` objects must live
     * at least until the next audio buffer gets processed.
     *
     * Technically a host can send more than one of these at a time, but in
     * practice every host will bundle all events in a single
     * `effProcessEvents()` call.
     */
    llvm::SmallVector<DynamicVstEvents, 1> next_audio_buffer_midi_events_;
    /**
     * Whether `next_audio_buffer_midi_events` should be cleared before
     * inserting new events.
     *
     * HACK: Normally we should be able to clear these immediately after the
     *       processing call, but Native Instruments' FM7 requires the last MIDI
     *       event to stay alive if there have not been any new MIDI events
     *       during the current processing cycle.
     */
    bool should_clear_midi_events_ = false;
    /**
     * Mutex for locking the above event queue, since recieving and processing
     * now happens in two different threads.
     */
    std::mutex next_buffer_midi_events_mutex_;

    /**
     * Used to allow the responses to host callbacks to be handled on the same
     * thread that originally made the callback. Luckily this is much rarer with
     * VST2 plugins than with VST3 plugins (presumably because plugins make
     * fewer GUI-related-ish callbacks), but it does happen.
     *
     * See `mutually_recursive_callbacks` and
     * `safe_mutually_recursive_requests` in the implementation file for more
     * information on the callbacks where we want to use
     * `mutual_recursion_.fork()` to send them in a new thread, and the
     * responses that have to be handled with `mutual_recursion_.handle()`.
     */
    MutualRecursionHelper<Win32Thread> mutual_recursion_;
};
