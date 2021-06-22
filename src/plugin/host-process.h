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

#include <thread>

#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/filesystem.hpp>
#include <boost/process/child.hpp>
#include <boost/process/extend.hpp>
#include <boost/process/io.hpp>

#include "../common/communication/common.h"
#include "../common/logging/common.h"
#include "../common/plugins.h"
#include "../common/serialization/common.h"
#include "utils.h"

/**
 * Encapsulates the behavior of launching a host process or connecting to an
 * existing one. This is needed because plugins groups require slightly
 * different handling. All derived classes are set up to pipe their STDOUT and
 * STDERR streams to the provided IO context instance.
 */
class HostProcess {
   public:
    virtual ~HostProcess() noexcept;

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

    /**
     * Simple helper function around `boost::process::child` that launches the
     * host application (`*.exe`) with some basic setup. This includes setting
     * up the asynchronous pipes for STDIO redirection, closing file descriptors
     * to prevent leaks, and wrapping everything in winedbg if we're compiling
     * with `-Dwith-winedbg=true`. Keep in mind that winedbg does not handle
     * arguments containing spaces, so most Windows paths will be split up into
     * multiple arugments.
     */
    template <typename... Args>
    boost::process::child launch_host(boost::filesystem::path host_path,
                                      Args&&... args) {
        return boost::process::child(
#ifdef WITH_WINEDBG
            // Use the terminal output or `$YABRIDGE_DEBUG_LOG` to get
            // yabridge's output, and use the printed target command in a GDB
            // session where you load the yabridge `.so` file you're trying to
            // debug
            "/usr/bin/winedbg", "--gdb", "--no-start",
            host_path.string() + ".so",
#else
            host_path,
#endif
            boost::process::std_out = stdout_pipe,
            boost::process::std_err = stderr_pipe,
            // NOTE: If the Wine process outlives the host, then it may cause
            //       issues if our process is still keeping the host's file
            //       descriptors alive that. This can prevent Ardour from
            //       restarting after an unexpected shutdown. Because of this we
            //       won't use `vfork()`, but instead we'll just manually close
            //       all non-STDIO file descriptors.
            // HACK: If the `disable_pipes` option is enabled, then we'll
            //       redirect the plugin's output to a file instead of using
            //       pipes to blend it in with the rest of yabridge's output.
            //       This is for some reason necessary for ujam's plugins and
            //       all other plugins made with Gorilla Engine to function.
            //       Otherwise they'll print a nondescriptive `JS_EXEC_FAILED`
            //       error message.
            boost::process::extend::on_exec_setup =
                [this](auto& /*executor*/) {
                    const int max_fds = static_cast<int>(sysconf(_SC_OPEN_MAX));
                    for (int fd = STDERR_FILENO + 1; fd < max_fds; fd++) {
                        close(fd);
                    }

                    // See above
                    if (config.disable_pipes) {
                        const int redirect_fd =
                            open(config.disable_pipes->c_str(),
                                 O_CREAT | O_APPEND | O_WRONLY, 0640);

                        assert(redirect_fd != -1);
                        dup2(redirect_fd, STDOUT_FILENO);
                        dup2(redirect_fd, STDERR_FILENO);
                        close(redirect_fd);
                    }
                },
            std::forward<Args>(args)...);
    }

   protected:
    /**
     * Initialize the host process by setting up the STDIO redirection.
     *
     * @param io_context The IO context that the STDIO redurection will be
     *   handled on.
     * @param logger The `Logger` instance the redirected STDIO streams will be
     *   written to.
     * @param sockets The socket endpoints that will be used for communication
     *   with the plugin. When the plugin shuts down, we'll close all of the
     *   sockets used by the plugin.
     */
    HostProcess(boost::asio::io_context& io_context,
                Logger& logger,
                const Configuration& config,
                Sockets& sockets);

    /**
     * The STDOUT stream of the Wine process we can forward to the logger.
     */
    patched_async_pipe stdout_pipe;
    /**
     * The STDERR stream of the Wine process we can forward to the logger.
     */
    patched_async_pipe stderr_pipe;

    /**
     * The current plugin instance's configuration.
     */
    const Configuration& config;

    /**
     * The associated sockets for the plugin we're hosting. This is used to
     * terminate the plugin.
     */
    Sockets& sockets;

   private:
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
     * @param config The configuration for this plugin instance.
     * @param sockets The socket endpoints that will be used for communication
     *   with the plugin. When the plugin shuts down, we'll close all of the
     *   sockets used by the plugin.
     * @param plugin_info Information about the plugin we're going to use. Used
     *   to retrieve the Wine prefix and the plugin's architecture.
     * @param host_request The information about the plugin we should launch a
     *   host process for. The values in the struct will be used as command line
     *   arguments.
     *
     * @throw std::runtime_error When `plugin_path` does not point to a valid
     *   32-bit or 64-bit .dll file.
     */
    IndividualHost(boost::asio::io_context& io_context,
                   Logger& logger,
                   const Configuration& config,
                   Sockets& sockets,
                   const PluginInfo& plugin_info,
                   const HostRequest& host_request);

    boost::filesystem::path path() override;
    bool running() override;
    void terminate() override;

   private:
    const PluginInfo& plugin_info;
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
     * @param config The configuration for this plugin instance. The group name
     *   will be retrieved from here.
     * @param sockets The socket endpoints that will be used for communication
     *   with the plugin. When the plugin shuts down, we'll close all of the
     *   sockets used by the plugin.
     * @param plugin_info Information about the plugin we're going to use. Used
     *   to retrieve the Wine prefix and the plugin's architecture.
     * @param host_request The information about the plugin we should launch a
     *   host process for. This object will be sent to the group host process.
     */
    GroupHost(boost::asio::io_context& io_context,
              Logger& logger,
              const Configuration& config,
              Sockets& sockets,
              const PluginInfo& plugin_info,
              const HostRequest& host_request);

    boost::filesystem::path path() override;
    bool running() noexcept override;
    void terminate() override;

   private:
    const PluginInfo& plugin_info;
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
