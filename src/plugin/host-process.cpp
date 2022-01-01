// yabridge: a Wine VST bridge
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

#include "host-process.h"

#include <boost/asio/read_until.hpp>
#include <boost/process/env.hpp>
#include <boost/process/start_dir.hpp>

#include "../common/utils.h"

namespace bp = boost::process;
namespace fs = boost::filesystem;

HostProcess::HostProcess(boost::asio::io_context& io_context,
                         Logger& logger,
                         const Configuration& config,
                         Sockets& sockets)
    : stdout_pipe(io_context),
      stderr_pipe(io_context),
      config(config),
      sockets(sockets),
      logger(logger) {
    // See the comment above the `on_exec_setup` in `launch_host()`
    if (config.disable_pipes) {
        logger.log("");
        logger.log("WARNING: All Wine output will be written to");
        logger.log("         '" + config.disable_pipes->string() + "'.");
        logger.log("");
    } else {
        // Print the Wine host's STDOUT and STDERR streams to the log file. This
        // should be done before trying to accept the sockets as otherwise we
        // will miss all output.
        logger.async_log_pipe_lines(stdout_pipe, stdout_buffer,
                                    "[Wine STDOUT] ");
        logger.async_log_pipe_lines(stderr_pipe, stderr_buffer,
                                    "[Wine STDERR] ");
    }
}

HostProcess::~HostProcess() noexcept {}

IndividualHost::IndividualHost(boost::asio::io_context& io_context,
                               Logger& logger,
                               const Configuration& config,
                               Sockets& sockets,
                               const PluginInfo& plugin_info,
                               const HostRequest& host_request)
    : HostProcess(io_context, logger, config, sockets),
      plugin_info(plugin_info),
      host_path(find_vst_host(plugin_info.native_library_path,
                              plugin_info.plugin_arch,
                              false)),
      host(
          launch_host(host_path,
                      plugin_type_to_string(host_request.plugin_type),
#if defined(WITH_WINEDBG) && defined(WINEDBG_LEGACY_ARGUMENT_QUOTING)
                      // Old versions of winedbg flattened all command line
                      // arguments to a single space separated Win32 command
                      // line, so we had to do our own quoting
                      "\"" + plugin_info.windows_plugin_path + "\"",
#else
                      host_request.plugin_path,
#endif
                      host_request.endpoint_base_dir,
                      // We pass this process' process ID as an argument so we
                      // can run a watchdog on the Wine plugin host process that
                      // shuts down the sockets after this process shuts down
                      std::to_string(getpid()),
                      bp::env = plugin_info.create_host_env())) {
#ifdef WITH_WINEDBG
    if (plugin_info.windows_plugin_path.string().find('"') !=
        std::string::npos) {
        logger.log(
            "Warning: plugin paths containing double quotes won't be properly "
            "escaped");
    }
#endif
}

fs::path IndividualHost::path() {
    return host_path;
}

bool IndividualHost::running() {
    // NOTE: `boost::process::child::running()` still considers zombies as
    //       running, so it's useless for our purposes.
    return pid_running(host.id());
}

void IndividualHost::terminate() {
    // NOTE: This technically shouldn't be needed, but in Wine 6.5 sending
    //       SIGKILL to a Wine process no longer terminates the threads spawned
    //       by that process, so if we don't manually close the sockets there
    //       will still be threads listening on those sockets which in turn also
    //       prevents us from joining our `std::jthread`s on the plugin side.
    sockets.close();

    host.terminate();
    // NOTE: This leaves a zombie, because Boost.Process will actually not call
    //       `wait()` after we have terminated the process.
    host.wait();
}

GroupHost::GroupHost(boost::asio::io_context& io_context,
                     Logger& logger,
                     const Configuration& config,
                     Sockets& sockets,
                     const PluginInfo& plugin_info,
                     const HostRequest& host_request)
    : HostProcess(io_context, logger, config, sockets),
      plugin_info(plugin_info),
      host_path(find_vst_host(plugin_info.native_library_path,
                              plugin_info.plugin_arch,
                              true)) {
    // When using plugin groups, we'll first try to connect to an existing group
    // host process and ask it to host our plugin. If no such process exists,
    // then we'll start a new process. In the event that multiple yabridge
    // instances simultaneously try to start a new group process for the same
    // group, then the first process to listen on the socket will win and all
    // other processes will exit. When a plugin's host process has exited, it
    // will try to connect to the socket once more in the case that another
    // process is now listening on it.
    const fs::path endpoint_base_dir = sockets.base_dir;
    const fs::path group_socket_path = generate_group_endpoint(
        *config.group, plugin_info.normalize_wine_prefix(),
        plugin_info.plugin_arch);
    const auto connect = [&io_context, host_request, endpoint_base_dir,
                          group_socket_path]() {
        boost::asio::local::stream_protocol::socket group_socket(io_context);
        group_socket.connect(group_socket_path.string());

        write_object(group_socket, host_request);
        const auto response = read_object<HostResponse>(group_socket);
        assert(response.pid > 0);
    };

    try {
        // Request an existing group host process to host our plugin
        connect();
    } catch (const boost::system::system_error&) {
        // In case we could not connect to the socket, then we'll start a
        // new group host process. This process is detached immediately
        // because it should run independently of this yabridge instance as
        // it will likely outlive it.
        bp::child group_host =
            launch_host(host_path, group_socket_path,
                        bp::env = plugin_info.create_host_env());
        group_host.detach();

        const pid_t group_host_pid = group_host.id();
        group_host_connect_handler =
            std::jthread([this, connect, group_host_pid]() {
                set_realtime_priority(true);
                pthread_setname_np(pthread_self(), "group-connect");

                using namespace std::literals::chrono_literals;

                // We'll first try to connect to the group host we just spawned
                // TODO: Replace this polling with inotify
                while (pid_running(group_host_pid)) {
                    std::this_thread::sleep_for(20ms);

                    try {
                        connect();
                        return;
                    } catch (const boost::system::system_error&) {
                        // Keep trying to connect until either connection gets
                        // accepted or the group host crashes
                    }
                }

                // When the group host exits before we can connect to it this
                // either means that there has been some kind of error (for
                // instance related to Wine), or that another process was able
                // to listen on the socket first. For the last case we'll try to
                // connect once more, before concluding that we failed.
                try {
                    connect();
                } catch (const boost::system::system_error&) {
                    startup_failed = true;
                }
            });
    }
}

fs::path GroupHost::path() {
    return host_path;
}

bool GroupHost::running() noexcept {
    // When we are unable to connect to a new or existing group host process,
    // then we'll consider the startup failed and we'll allow the initialization
    // process to terminate.
    return !startup_failed;
}

void GroupHost::terminate() {
    // There's no need to manually terminate group host processes as they will
    // shut down automatically after all plugins have exited. Manually closing
    // the sockets will cause the associated plugin to exit.
    sockets.close();
}
