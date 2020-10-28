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

#include "host-process.h"

#include <boost/asio/read_until.hpp>
#include <boost/process/env.hpp>
#include <boost/process/io.hpp>
#include <boost/process/start_dir.hpp>

#include "../common/communication.h"

namespace bp = boost::process;
namespace fs = boost::filesystem;

/**
 * Simple helper function around `boost::process::child` that launches the host
 * application (`*.exe`) wrapped in winedbg if compiling with
 * `-Duse-winedbg=true`. Keep in mind that winedbg does not handle arguments
 * containing spaces, so most Windows paths will be split up into multiple
 * arugments.
 */
template <typename... Args>
bp::child launch_host(fs::path host_path, Args&&... args) {
    return bp::child(
#ifdef WITH_WINEDBG
        // This is set up for KDE Plasma. Other desktop environments and
        // window managers require some slight modifications to spawn a
        // detached terminal emulator.
        "/usr/bin/kstart5", "konsole", "--", "-e", "winedbg", "--gdb",
        host_path.string() + ".so",
#else
        host_path,
#endif
        // We'll use vfork() instead of fork to avoid potential issues with
        // inheriting file descriptors
        // https://github.com/robbert-vdh/yabridge/issues/45
        bp::posix::use_vfork, std::forward<Args>(args)...);
}

HostProcess::HostProcess(boost::asio::io_context& io_context, Logger& logger)
    : stdout_pipe(io_context), stderr_pipe(io_context), logger(logger) {
    // Print the Wine host's STDOUT and STDERR streams to the log file. This
    // should be done before trying to accept the sockets as otherwise we will
    // miss all output.
    async_log_pipe_lines(stdout_pipe, stdout_buffer, "[Wine STDOUT] ");
    async_log_pipe_lines(stderr_pipe, stderr_buffer, "[Wine STDERR] ");
}

void HostProcess::async_log_pipe_lines(patched_async_pipe& pipe,
                                       boost::asio::streambuf& buffer,
                                       std::string prefix) {
    boost::asio::async_read_until(
        pipe, buffer, '\n',
        [&, prefix](const boost::system::error_code& error, size_t) {
            // When we get an error code then that likely means that the pipe
            // has been clsoed and we have reached the end of the file
            if (error.failed()) {
                return;
            }

            std::string line;
            std::getline(std::istream(&buffer), line);
            logger.log(prefix + line);

            async_log_pipe_lines(pipe, buffer, prefix);
        });
}

IndividualHost::IndividualHost(boost::asio::io_context& io_context,
                               Logger& logger,
                               fs::path plugin_path,
                               const Sockets<std::jthread>& sockets)
    : HostProcess(io_context, logger),
      plugin_arch(find_vst_architecture(plugin_path)),
      host_path(find_vst_host(plugin_arch, false)),
      host(launch_host(host_path,
#ifdef WITH_WINEDBG
                       plugin_path.filename(),
#else
                       plugin_path,
#endif
                       sockets.base_dir,
                       bp::env = set_wineprefix(),
                       bp::std_out = stdout_pipe,
                       bp::std_err = stderr_pipe
#ifdef WITH_WINEDBG
                       ,  // winedbg has no reliable way to escape spaces, so
                          // we'll start the process in the plugin's directory
                       bp::start_dir = plugin_path.parent_path()
#endif
                           )) {
#ifdef WITH_WINEDBG
    if (plugin_path.filename().string().find(' ') != std::string::npos) {
        logger.log("Warning: winedbg does not support paths containing spaces");
    }
#endif
}

PluginArchitecture IndividualHost::architecture() {
    return plugin_arch;
}

fs::path IndividualHost::path() {
    return host_path;
}

bool IndividualHost::running() {
    return host.running();
}

void IndividualHost::terminate() {
    host.terminate();
    host.wait();
}

GroupHost::GroupHost(boost::asio::io_context& io_context,
                     Logger& logger,
                     fs::path plugin_path,
                     Sockets<std::jthread>& sockets,
                     std::string group_name)
    : HostProcess(io_context, logger),
      plugin_arch(find_vst_architecture(plugin_path)),
      host_path(find_vst_host(plugin_arch, true)),
      sockets(sockets) {
#ifdef WITH_WINEDBG
    if (plugin_path.string().find(' ') != std::string::npos) {
        logger.log("Warning: winedbg does not support paths containing spaces");
    }
#endif

    // When using plugin groups, we'll first try to connect to an existing group
    // host process and ask it to host our plugin. If no such process exists,
    // then we'll start a new process. In the event that two yabridge instances
    // simultaneously try to start a new group process for the same group, then
    // the last process to connect to the socket will terminate gracefully and
    // the first process will handle the connections for both yabridge
    // instances.
    const bp::environment host_env = set_wineprefix();
    fs::path wine_prefix;
    if (auto wine_prefix_envvar = host_env.find("WINEPREFIX");
        wine_prefix_envvar != host_env.end()) {
        // This is a bit ugly, but Boost.Process's environment does not have a
        // graceful way to check for empty environment variables in const
        // qualified environments
        wine_prefix = wine_prefix_envvar->to_string();
    } else {
        // Fall back to `~/.wine` if this has not been set or detected. This
        // would happen if the plugin's .dll file is not inside of a Wine
        // prefix. If this happens, then the Wine instance will be launched in
        // the default Wine prefix, so we should reflect that here.
        wine_prefix = fs::path(host_env.at("HOME").to_string()) / ".wine";
    }

    const fs::path endpoint_base_dir = sockets.base_dir;
    const fs::path group_socket_path =
        generate_group_endpoint(group_name, wine_prefix, plugin_arch);
    try {
        // Request the existing group host process to host our plugin, and store
        // the PID of that process so we'll know if it has crashed
        boost::asio::local::stream_protocol::socket group_socket(io_context);
        group_socket.connect(group_socket_path.string());

        write_object(
            group_socket,
            GroupRequest{.plugin_path = plugin_path.string(),
                         .endpoint_base_dir = endpoint_base_dir.string()});
        const auto response = read_object<GroupResponse>(group_socket);

        host_pid = response.pid;
    } catch (const boost::system::system_error&) {
        // In case we could not connect to the socket, then we'll start a
        // new group host process. This process is detached immediately
        // because it should run independently of this yabridge instance as
        // it will likely outlive it.
        bp::child group_host =
            launch_host(host_path, group_socket_path, bp::env = host_env,
                        bp::std_out = stdout_pipe, bp::std_err = stderr_pipe);
        host_pid = group_host.id();
        group_host.detach();

        // We now want to connect to the socket the in the exact same way as
        // above. The only problem is that it may take some time for the
        // process to start depending on Wine's current state. We'll defer
        // this to a thread so we can finish the rest of the startup in the
        // meantime.
        group_host_connect_handler = std::jthread([&, group_socket_path,
                                                   plugin_path,
                                                   endpoint_base_dir]() {
            using namespace std::literals::chrono_literals;

            // TODO: Replace this polling with inotify
            while (running()) {
                std::this_thread::sleep_for(20ms);

                try {
                    // This is the exact same connection sequence as above
                    boost::asio::local::stream_protocol::socket group_socket(
                        io_context);
                    group_socket.connect(group_socket_path.string());

                    write_object(
                        group_socket,
                        GroupRequest{
                            .plugin_path = plugin_path.string(),
                            .endpoint_base_dir = endpoint_base_dir.string()});
                    const auto response =
                        read_object<GroupResponse>(group_socket);

                    // If two group processes started at the same time, than the
                    // first one will be the one to respond to the host request
                    host_pid = response.pid;
                    return;
                } catch (const boost::system::system_error&) {
                    // Keep trying to connect until either connection gets
                    // accepted or the group host crashes
                }
            }
        });
    }
}

PluginArchitecture GroupHost::architecture() {
    return plugin_arch;
}

fs::path GroupHost::path() {
    return host_path;
}

bool GroupHost::running() {
    // With regular individually hosted plugins we can simply check whether the
    // process is still running, however Boost.Process does not allow you to do
    // the same thing for a process that's not a direct child if this process.
    // When using plugin groups we'll have to manually check whether the PID
    // returned by the group host process is still active. We sadly can't use
    // `kill()` for this as that provides no way to distinguish between active
    // processes and zombies, and a terminated group host process will always be
    // left as a zombie process. If the process is active, then
    // `/proc/<pid>/{cwd,exe,root}` will be valid symlinks.
    try {
        fs::canonical("/proc/" + std::to_string(host_pid) + "/exe");
        return true;
    } catch (const fs::filesystem_error&) {
        return false;
    }
}

void GroupHost::terminate() {
    // There's no need to manually terminate group host processes as they will
    // shut down automatically after all plugins have exited. Manually closing
    // the dispatch socket will cause the associated plugin to exit.
    sockets.host_vst_dispatch.close();
}
