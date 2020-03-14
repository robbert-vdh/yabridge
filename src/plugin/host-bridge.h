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
#include <boost/asio/streambuf.hpp>
#include <boost/process/async_pipe.hpp>
#include <boost/process/child.hpp>
#include <thread>

#include "../common/logging.h"

/**
 * Boost 1.72 was released with a known breaking bug caused by a missing
 * typedef: https://github.com/boostorg/process/issues/116.
 *
 * Luckily this is easy to fix since it's not really possible to downgrade Boost
 * as it would break other applications.
 *
 * Check if this is still needed for other distros after Arch starts packaging
 * Boost 1.73.
 */
class patched_async_pipe : public boost::process::async_pipe {
   public:
    using boost::process::async_pipe::async_pipe;

    typedef typename handle_type::executor_type executor_type;
};

/**
 * This handles the communication between the Linux native VST plugin and the
 * Wine VST host. The functions below should be used as callback functions in an
 * `AEffect` object.
 */
class HostBridge {
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
    HostBridge(audioMasterCallback host_callback);

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
     * Ask the VST plugin to process audio for us. This should also be used for
     * the deprecated 'process' function.
     */
    void process_replacing(AEffect* plugin,
                           float** inputs,
                           float** outputs,
                           int sample_frames);
    float get_parameter(AEffect* plugin, int index);
    void set_parameter(AEffect* plugin, int index, float value);

    /**
     * The path to `yabridge-host.exe`.
     */
    const boost::filesystem::path vst_host_path;
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

    /**
     * The VST hsot can query a plugin for arbitrary binary data such as
     * presets. It will expect the plugin to write back a pointer that points to
     * that data. This vector is where we store the chunk data for the last
     * `effGetChunk` event.
     */
    std::vector<uint8_t> chunk_data;

   private:
    /**
     * Write output from an async pipe to the log on a line by line basis.
     * Useful for logging the Wine process's STDOUT and STDERR streams.
     *
     * @param pipe The pipe to read from.
     * @param buffer The stream buffer to write to.
     * @param prefix Text to prepend to the line before writing to the log.
     */
    void async_log_pipe_lines(patched_async_pipe& pipe,
                              boost::asio::streambuf& buffer,
                              std::string prefix = "");

    boost::asio::io_context io_context;
    boost::asio::local::stream_protocol::endpoint socket_endpoint;
    boost::asio::local::stream_protocol::acceptor socket_acceptor;

    // The naming convention for these sockets is `<from>_<to>_<event>`. For
    // instance the socket named `host_vst_dispatch` forwards
    // `AEffect.dispatch()` calls from the native VST host to the Windows VST
    // plugin (through the Wine VST host).
    boost::asio::local::stream_protocol::socket host_vst_dispatch;
    boost::asio::local::stream_protocol::socket vst_host_callback;
    /**
     * Used for both `getParameter` and `setParameter` since they mostly
     * overlap.
     *
     * TODO: Verify that these 100% won't be called simultanously since that
     *       would cause a race condition.
     */
    boost::asio::local::stream_protocol::socket host_vst_parameters;
    boost::asio::local::stream_protocol::socket host_vst_process_replacing;

    /**
     * This socket only handles updates of the `AEffect` struct instead of
     * passing through function calls. It's also used during initialization to
     * pass the Wine plugin's information to the host.
     */
    boost::asio::local::stream_protocol::socket vst_host_aeffect;

    /**
     * The thread that handles host callbacks.
     */
    std::thread host_callback_handler;

    /**
     * The callback function passed by the host to the VST plugin instance.
     */
    audioMasterCallback host_callback_function;
    Logger logger;

    boost::asio::streambuf wine_stdout_buffer;
    boost::asio::streambuf wine_stderr_buffer;
    /**
     * The STDOUT stream of the Wine process we can forward to the logger.
     */
    patched_async_pipe wine_stdout;
    /**
     * The STDERR stream of the Wine process we can forward to the logger.
     */
    patched_async_pipe wine_stderr;
    /**
     * Runs the Boost.Asio `io_context` thread for logging the Wine process
     * STDOUT and STDERR messages.
     */
    std::thread wine_io_handler;

    /**
     * The Wine process hosting the Windows VST plugin.
     */
    boost::process::child vst_host;

    /**
     * A scratch buffer for sending and receiving data during `process` and
     * `processReplacing` calls.
     */
    std::vector<uint8_t> process_buffer;
};
