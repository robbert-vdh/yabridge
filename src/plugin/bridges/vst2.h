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

#include <vestige/aeffectx.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <mutex>
#include <thread>

#include "../../common/communication/vst2.h"
#include "../../common/configuration.h"
#include "../../common/logging.h"
#include "../host-process.h"

/**
 * This handles the communication between the Linux native VST2 plugin and the
 * Wine VST host. The functions below should be used as callback functions in an
 * `AEffect` object.
 *
 * The naming scheme of all of these 'bridge' classes is `<type>{,Plugin}Bridge`
 * for greppability reasons. The `Plugin` infix is added on the native plugin
 * side.
 */
class Vst2PluginBridge {
   public:
    /**
     * Initializes the Wine VST bridge. This sets up the sockets for event
     * handling.
     *
     * @param host_callback The callback function passed to the VST plugin by
     *   the host.
     *
     * @throw std::runtime_error Thrown when the VST host could not be found, or
     *   if it could not locate and load a VST .dll file.
     */
    Vst2PluginBridge(audioMasterCallback host_callback);

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
     * The configuration for this instance of yabridge. Set based on the values
     * from a `yabridge.toml`, if it exists.
     *
     * @see ./utils.h:load_config_for
     */
    Configuration config;

    /**
     * The path to the .dll being loaded in the Wine VST host.
     */
    const boost::filesystem::path vst_plugin_path;

    /**
     * This AEffect struct will be populated using the data passed by the Wine
     * VST host during initialization and then passed as a pointer to the Linux
     * native VST host from the Linux VST plugin's entry point.
     */
    AEffect plugin;

   private:
    /**
     * Format and log all relevant debug information during initialization.
     */
    void log_init_message();

    boost::asio::io_context io_context;
    Sockets<std::jthread> sockets;

    /**
     * The thread that handles host callbacks.
     */
    std::jthread host_callback_handler;

    /**
     * A mutex to prevent multiple simultaneous calls to `getParameter()` and
     * `setParameter()`. This likely won't happen, but better safe than sorry.
     * For `dispatch()` and `audioMaster()` there's some more complex logic for
     * this in `EventHandler`.
     */
    std::mutex parameters_mutex;

    /**
     * The callback function passed by the host to the VST plugin instance.
     */
    audioMasterCallback host_callback_function;

    /**
     * The logging facility used for this instance of yabridge. See
     * `Logger::create_from_env()` for how this is configured.
     *
     * @see Logger::create_from_env
     */
    Logger logger;

    /**
     * The version of Wine currently in use. Used in the debug output on plugin
     * startup.
     */
    const std::string wine_version;

    /**
     * The Wine process hosting the Windows VST plugin.
     *
     * @see launch_vst_host
     */
    std::unique_ptr<HostProcess> vst_host;

    /**
     * A thread used during the initialisation process to terminate listening on
     * the sockets if the Wine process cannot start for whatever reason. This
     * has to be defined here instead of in the constructor we can't simply
     * detach the thread as it has to check whether the VST host is still
     * running.
     */
    std::jthread host_guard_handler;

    /**
     * Whether this process runs with realtime priority. We'll set this _after_
     * spawning the Wine process because from my testing running wineserver with
     * realtime priority can actually increase latency.
     */
    bool has_realtime_priority;

    /**
     * Runs the Boost.Asio `io_context` thread for logging the Wine process
     * STDOUT and STDERR messages.
     */
    std::jthread wine_io_handler;

    /**
     * A scratch buffer for sending and receiving data during `process`,
     * `processReplacing` and `processDoubleReplacing` calls.
     */
    std::vector<uint8_t> process_buffer;

    /**
     * The VST host can query a plugin for arbitrary binary data such as
     * presets. It will expect the plugin to write back a pointer that points to
     * that data. This vector is where we store the chunk data for the last
     * `effGetChunk` event.
     */
    std::vector<uint8_t> chunk_data;
    /**
     * The VST host will expect to be returned a pointer to a struct that stores
     * the dimensions of the editor window.
     */
    VstRect editor_rectangle;

    /**
     * Sending MIDI events sent to the host by the plugin using
     * `audioMasterProcessEvents` function has to be done during the processing
     * function. If they are sent during any other time or from another thread,
     * then the host will just discard them. Because we're receiving our host
     * callbacks on a separate thread, we have to temporarily store any events
     * we receive so we can send them to the host at the end of
     * `process_replacing()`.
     */
    std::vector<DynamicVstEvents> incoming_midi_events;
    /**
     * Mutex for locking the above event queue, since recieving and processing
     * now happens in two different threads.
     */
    std::mutex incoming_midi_events_mutex;
};
