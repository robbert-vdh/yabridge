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

// Boost.Process's auto detection for vfork() support doesn't seem to work
#define BOOST_POSIX_HAS_VFORK 1

#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/filesystem.hpp>
#include <boost/process/child.hpp>
#include <thread>

// TODO: Those host process implementation now directly uses the Vst2Sockets and
//       thus requires `communication/vst2.h`. We should create a simple common
//       interface for this instead.
#include "../common/communication/vst2.h"
#include "../common/logging.h"
#include "utils.h"

/**
 * Encapsulates the behavior of launching a host process or connecting to an
 * existing one. This is needed because plugins groups require slightly
 * different handling. All derived classes are set up to pipe their STDOUT and
 * STDERR streams to the provided IO context instance.
 */
class HostProcess {
   public:
    virtual ~HostProcess(){};

    /**
     * Return the architecture of the plugin we are loading, i.e. whether it is
     * 32-bit or 64-bit.
     */
    virtual PluginArchitecture architecture() = 0;

    /**
     * Return the full path to the host application in use. The host application
     * is chosen depending on the architecture of the plugin's DLL file and on
     * the hosting mode.
     */
    virtual boost::filesystem::path path() = 0;

    /**
     * Return true if the host process is still running. Used during startup to
     * abort connecting to sockets if the Wine process has crashed.
     */
    virtual bool running() = 0;

    /**
     * Kill the process or cause the plugin that's being hosted to exit.
     */
    virtual void terminate() = 0;

   protected:
    /**
     * Initialize the host process by setting up the STDIO redirection.
     *
     * @param io_context The IO context that the STDIO redurection will be
     *   handled on.
     * @param logger The `Logger` instance the redirected STDIO streams will be
     *   written to.
     */
    HostProcess(boost::asio::io_context& io_context, Logger& logger);

    /**
     * The STDOUT stream of the Wine process we can forward to the logger.
     */
    patched_async_pipe stdout_pipe;
    /**
     * The STDERR stream of the Wine process we can forward to the logger.
     */
    patched_async_pipe stderr_pipe;

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

    /**
     * The logger the Wine output will be written to.
     */
    Logger& logger;

    boost::asio::streambuf stdout_buffer;
    boost::asio::streambuf stderr_buffer;
};

/**
 * Launch a group host process for hosting a single plugin.
 */
class IndividualHost : public HostProcess {
   public:
    /**
     * Start a host process that loads the plugin and connects back to this
     * yabridge instance over the specified socket.
     *
     * @param io_context The IO context that the STDIO redurection will be
     *   handled on.
     * @param logger The `Logger` instance the redirected STDIO streams will be
     *   written to.
     * @param sockets The socket endpoints that will be used for communication
     *   with the plugin.
     *
     * @throw std::runtime_error When `plugin_path` does not point to a valid
     *   32-bit or 64-bit .dll file.
     */
    IndividualHost(boost::asio::io_context& io_context,
                   Logger& logger,
                   boost::filesystem::path plugin_path,
                   const Sockets<std::jthread>& sockets);

    PluginArchitecture architecture() override;
    boost::filesystem::path path() override;
    bool running() override;
    void terminate() override;

   private:
    PluginArchitecture plugin_arch;
    boost::filesystem::path host_path;
    boost::process::child host;
};

/**
 * Either launch a new group host process, or connect to an existing one. This
 * will first try to connect to the plugin group's socket (determined based on
 * group name, Wine prefix and architecture). If that fails, it will launch a
 * new, detached group host process. This will likely outlive this plugin
 * instance if multiple instances of yabridge using the same plugin group are in
 * use. In the event that two yabridge instances are initialized at the same
 * time and both instances spawn their own group host process, then the later
 * one will simply terminate gracefully after it fails to listen on the socket.
 */
class GroupHost : public HostProcess {
   public:
    /**
     * Start a new group host process or connect to an existing one. The actual
     * host request is deferred until the process has actually started using a
     * thread.
     *
     * @param io_context The IO context that the STDIO redurection will be
     *   handled on.
     * @param logger The `Logger` instance the redirected STDIO streams will be
     *   written to.
     * @param sockets The socket endpoints that will be used for communication
     *   with the plugin. When the plugin shuts down, we'll terminate the
     *   dispatch socket contained in this object.
     * @param group_name The name of the plugin group.
     */
    GroupHost(boost::asio::io_context& io_context,
              Logger& logger,
              boost::filesystem::path plugin_path,
              Sockets<std::jthread>& socket_endpoint,
              std::string group_name);

    PluginArchitecture architecture() override;
    boost::filesystem::path path() override;
    bool running() override;
    void terminate() override;

   private:
    PluginArchitecture plugin_arch;
    boost::filesystem::path host_path;

    /**
     * We want to either connect to an existing group host process, or spawn a
     * new one. This can result in some interesting scenarios when multiple
     * plugins within the same plugin host get initialized at once (e.g. when
     * loading a project). On startup we'll go through the following sequence:
     *
     * 1. Try to connect to an existing group host process.
     * 2. Spawn a new group host process and connect to it. When multiple
     *    plugins launch at the same time the first to start listening on the
     *    socket wins and the other processes will shut down gracefully.
     * 3.  When the group host process exits, try to connect again (potentially
     *     to a group host process spawned by another instance).
     *
     * When this last step also fails, then we'll say that startup has failed
     * and we will terminate the plugin initialization process.
     */
    std::atomic_bool startup_failed;

    /**
     * The associated sockets for the plugin we're hosting. This is used to
     * terminate the plugin.
     */
    Sockets<std::jthread>& sockets;

    /**
     * A thread that waits for the group host to have started and then ask it to
     * host our plugin. This is used to defer the request since it may take a
     * little while until the group host process is up and running. This way we
     * don't have to delay the rest of the initialization process.
     *
     * TODO: Replace the polling with inotify to prevent delays and to reduce
     *       wasting resources
     */
    std::jthread group_host_connect_handler;
};
