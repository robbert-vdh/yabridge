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

#include <vestige/aeffectx.h>

#include <asio/io_context.hpp>
#include <thread>

#include "../../common/communication/vst2.h"
#include "../../common/logging/vst2.h"
#include "common.h"

/**
 * This handles the communication between the Linux native VST2 plugin and the
 * Wine VST host. The functions below should be used as callback functions in an
 * `AEffect` object.
 *
 * The naming scheme of all of these 'bridge' classes is `<type>{,Plugin}Bridge`
 * for greppability reasons. The `Plugin` infix is added on the native plugin
 * side.
 */
class Vst2PluginBridge : PluginBridge<Vst2Sockets<std::jthread>> {
   public:
    /**
     * Initializes the Wine plugin bridge. This sets up the sockets for event
     * handling.
     *
     * @param plugin_path The path to the **native** plugin library `.so` file.
     *   This is used to determine the path to the Windows plugin library we
     *   should load. For directly loaded bridges this should be
     *   `get_this_file_location()`. Chainloaded plugins should use the path of
     *   the chainloader copy instead.
     * @param host_callback The callback function passed to the VST plugin by
     *   the host.
     *
     * @throw std::runtime_error Thrown when the VST host could not be found, or
     *   if it could not locate and load a VST .dll file.
     */
    Vst2PluginBridge(const ghc::filesystem::path& plugin_path,
                     audioMasterCallback host_callback);

    /**
     * Terminate the Wine plugin host process and drop all work when the module
     * gets unloaded.
     */
    ~Vst2PluginBridge() noexcept override;

    // The four below functions are the handlers from the VST2 API. They are
    // called through proxy functions in `plugin.cpp`.

    /**
     * Handle an event sent by the VST host. Most of these opcodes will be
     * passed through to the winelib VST host.
     */
    intptr_t dispatch(AEffect* plugin,
                      int opcode,
                      int index,
                      intptr_t value,
                      void* data,
                      float option);
    /**
     * This is the old, accumulative version of `processReplacing()`. As far as
     * I'm aware no host from the last 20 years will use this (since it's not
     * very practical), but we have to support this anyways. Because this is not
     * used, we'll just reuse your `process_replacing()` implementation (which
     * actually falls back to `process()` if the plugin somehow does not support
     * the former).
     */
    void process(AEffect* plugin,
                 float** inputs,
                 float** outputs,
                 int sample_frames);
    /**
     * Ask the VST plugin to process audio for us. If the plugin somehow does
     * not support `processReplacing()` and only supports the old `process()`
     * function, then this will be handled implicitely in
     * `Vst2Bridge::handle_process_replacing()`.
     */
    void process_replacing(AEffect* plugin,
                           float** inputs,
                           float** outputs,
                           int sample_frames);
    /**
     * The same as `Vst2PluginBridge::process_replacing`, but for double
     * precision audio. Support for this on both the plugin and host side is
     * pretty rare, but REAPER supports it. This reuses the same infrastructure
     * as `process_replacing` is using since the host will only call one or the
     * other.
     */
    void process_double_replacing(AEffect* plugin,
                                  double** inputs,
                                  double** outputs,
                                  int sample_frames);
    float get_parameter(AEffect* plugin, int index);
    void set_parameter(AEffect* plugin, int index, float value);

    /**
     * Process audio and handle plugin-generated MIDI events afterwards.
     *
     * @tparam T The sample type. Should be either `float` for single preceision
     *   audio processing called through `processReplacing`, or `double` for
     *   double precision audio through `processDoubleReplacing`.
     * @tparam replacing Whether or not `outputs` should be replaced by the new
     *   processed audio. This is the normal behaviour for `processReplacing()`.
     *   If this is set to `false` then the results are added to the existing
     *   values in `outputs`. No host will use this last behaviour anymore, but
     *   it's part of the VST2.4 spec so we have to support it.
     *
     * @see Vst2PluginBridge::process_replacing
     * @see Vst2PluginBridge::process_double_replacing
     */
    template <typename T, bool replacing>
    void do_process(T** inputs, T** outputs, int sample_frames);

    /**
     * This AEffect struct will be populated using the data passed by the Wine
     * VST host during initialization and then passed as a pointer to the Linux
     * native VST host from the Linux VST plugin's entry point.
     */
    AEffect plugin_;

   private:
    /**
     * The thread that handles host callbacks.
     */
    std::jthread host_callback_handler_;

    /**
     * A mutex to prevent multiple simultaneous calls to `getParameter()` and
     * `setParameter()`. This likely won't happen, but better safe than sorry.
     * For `dispatch()` and `audioMaster()` there's some more complex logic for
     * this in `Vst2EventHandler`.
     */
    std::mutex parameters_mutex_;

    /**
     * The callback function passed by the host to the VST plugin instance.
     */
    audioMasterCallback host_callback_function_;

    /**
     * The logging facility used for this instance of yabridge. Wraps around
     * `PluginBridge::generic_logger`.
     */
    Vst2Logger logger_;

    /**
     * A shared memory object that contains both the input and output audio
     * buffers. This is first configured on the Wine plugin host side during
     * `effMainsChanged` and then replicated on the plugin side. This way we
     * reduce the amount of copying during audio processing to only two copies.
     * We'll write the input audio to this buffer and send the process request
     * to the Wine plugin host. There the Windows VST2 plugin will then read
     * from the buffer and write its results to the same buffer. We can then
     * write those results back to the host.
     *
     * This will be a nullopt until `effMainsChanged` has been called.
     */
    std::optional<AudioShmBuffer> process_buffers_;

    /**
     * We'll periodically synchronize the Wine host's audio thread priority with
     * that of the host. Since the overhead from doing so does add up, we'll
     * only do this every once in a while.
     */
    time_t last_audio_thread_priority_synchronization_ = 0;

    /**
     * The VST host can query a plugin for arbitrary binary data such as
     * presets. It will expect the plugin to write back a pointer that points to
     * that data. This vector is where we store the chunk data for the last
     * `effGetChunk` event.
     */
    std::vector<uint8_t> chunk_data_;
    /**
     * The VST host will expect to be returned a pointer to a struct that stores
     * the dimensions of the editor window.
     */
    VstRect editor_rectangle_;

    /**
     * Sending MIDI events sent to the host by the plugin using
     * `audioMasterProcessEvents` function has to be done during the processing
     * function. If they are sent during any other time or from another thread,
     * then the host will just discard them. Because we're receiving our host
     * callbacks on a separate thread, we have to temporarily store any events
     * we receive so we can send them to host on the audio thread at the end of
     * `process_replacing()`.
     */
    llvm::SmallVector<DynamicVstEvents, 1> incoming_midi_events_;
    /**
     * Mutex for locking the above event queue, since recieving and processing
     * now happens in two different threads.
     */
    std::mutex incoming_midi_events_mutex_;

    /**
     * REAPER requires us to call `audioMasterSizeWidnow()` from the same thread
     * that's calling `effEditIdle()`. If we call this from any other thread,
     * then the FX window won't be resized. To accommodate for this, we'll store
     * the width and the height passed to the last call to
     * `audioMasterSizeWindow`. If this contains a value, we'll then call
     * `audioMasterSizeWindow()` with the new size during `effEditIdle()`.
     */
    std::optional<std::pair<int, int>> incoming_resize_;
    std::mutex incoming_resize_mutex_;
};
