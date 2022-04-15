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

#include "host-process.h"

#include <asio/read_until.hpp>

#include "../common/utils.h"

namespace fs = ghc::filesystem;

HostProcess::HostProcess(asio::io_context& io_context, Sockets& sockets)
    : sockets_(sockets), stdout_pipe_(io_context), stderr_pipe_(io_context) {}

HostProcess::~HostProcess() noexcept {}

Process::Handle HostProcess::launch_host(
    const ghc::filesystem::path& host_path,
    std::initializer_list<std::string> args,
    Logger& logger,
    const Configuration& config,
    const PluginInfo& plugin_info) {
#ifdef WITH_WINEDBG
    // This is set up for KDE Plasma. Other desktop environments and window
    // managers require some slight modifications to spawn a detached terminal
    // emulator. Alternatively, you can spawn `winedbg` with the `--no-start`
    // option to launch a gdb server and then connect to it from another
    // terminal.
    Process child("kstart5");
    child.arg("konsole").arg("--").arg("-e").arg("winedbg").arg("--gdb");
#ifdef WINEDBG_LEGACY_ARGUMENT_QUOTING
    // Note the double quoting here. Old versions of winedbg didn't
    // respect `argv` and instead expected a pre-quoted Win32 command
    // line as its arguments.
    child.arg("\"" + host_path.string() + ".so\""),
#else
    child.arg(host_path.string() + ".so");
#endif  // WINEDBG_LEGACY_ARGUMENT_QUOTING
#else
    Process child(host_path);
#endif  // WITH_WINEDBG

        // What's up with this indentation
        for (const auto& arg : args) {
        child.arg(arg);
    }

    child.environment(plugin_info.create_host_env());
    Process::Handle child_handle = std::visit(
        overload{
            [](Process::Handle handle) -> Process::Handle { return handle; },
            [&host_path](const Process::CommandNotFound&) -> Process::Handle {
                throw std::runtime_error("Could not launch '" +
                                         host_path.string() +
                                         "', command not found");
            },
            [](const std::error_code& err) -> Process::Handle {
                throw std::runtime_error("Error spawning Wine process: " +
                                         err.message());
            },
        },
        // HACK: If the `disable_pipes` option is enabled, then we'll redirect
        //       the plugin's output to a file instead of using pipes to blend
        //       it in with the rest of yabridge's output. This is for some
        //       reason necessary for ujam's plugins and all other plugins made
        //       with Gorilla Engine to function. Otherwise they'll print a
        //       nondescriptive `JS_EXEC_FAILED` error message.
        config.disable_pipes
            ? child.spawn_child_redirected(*config.disable_pipes)
            : child.spawn_child_piped(stdout_pipe_, stderr_pipe_));

    // See the above comment
    if (config.disable_pipes) {
        logger.log("");
        logger.log("WARNING: All Wine output will be written to");
        logger.log("         '" + config.disable_pipes->string() + "'.");
        logger.log("");
    } else {
        // Print the Wine host's STDOUT and STDERR streams to the log file. This
        // should be done before trying to accept the sockets as otherwise we
        // will miss all output.
        logger.async_log_pipe_lines(stdout_pipe_, stdout_buffer_,
                                    "[Wine STDOUT] ");
        logger.async_log_pipe_lines(stderr_pipe_, stderr_buffer_,
                                    "[Wine STDERR] ");
    }

    return child_handle;
}

IndividualHost::IndividualHost(asio::io_context& io_context,
                               Logger& logger,
                               const Configuration& config,
                               Sockets& sockets,
                               const PluginInfo& plugin_info,
                               const HostRequest& host_request)
    : HostProcess(io_context, sockets),
      plugin_info_(plugin_info),
      host_path_(find_vst_host(plugin_info.native_library_path_,
                               plugin_info.plugin_arch_,
                               false)),
      handle_(launch_host(
          host_path_,
          {
              plugin_type_to_string(host_request.plugin_type),
#if defined(WITH_WINEDBG) && defined(WINEDBG_LEGACY_ARGUMENT_QUOTING)
                  // Old versions of winedbg flattened all command line
                  // arguments to a single space separated Win32 command line,
                  // so we had to do our own quoting
                  "\"" + plugin_info.windows_plugin_path + "\"",
#else
                  host_request.plugin_path,
#endif
                  host_request.endpoint_base_dir,
                  // We pass this process' process ID as an argument so we can
                  // run a watchdog on the Wine plugin host process that shuts
                  // down the sockets after this process shuts down
                  std::to_string(getpid())
          },
          logger,
          config,
          plugin_info)) {
#ifdef WITH_WINEDBG
    if (plugin_info.windows_plugin_path_.string().find('"') !=
        std::string::npos) {
        logger.log(
            "Warning: plugin paths containing double quotes won't be properly "
            "escaped");
    }
#endif
}

fs::path IndividualHost::path() {
    return host_path_;
}

bool IndividualHost::running() {
    return handle_.running();
}

void IndividualHost::terminate() {
    // NOTE: This technically shouldn't be needed, but in Wine 6.5 sending
    //       SIGKILL to a Wine process no longer terminates the threads spawned
    //       by that process, so if we don't manually close the sockets there
    //       will still be threads listening on those sockets which in turn also
    //       prevents us from joining our `std::jthread`s on the plugin side.
    sockets_.close();

    // This will also reap the terminated process
    handle_.terminate();
}

GroupHost::GroupHost(asio::io_context& io_context,
                     Logger& logger,
                     const Configuration& config,
                     Sockets& sockets,
                     const PluginInfo& plugin_info,
                     const HostRequest& host_request)
    : HostProcess(io_context, sockets),
      plugin_info_(plugin_info),
      host_path_(find_vst_host(plugin_info.native_library_path_,
                               plugin_info.plugin_arch_,
                               true)) {
    // When using plugin groups, we'll first try to connect to an existing group
    // host process and ask it to host our plugin. If no such process exists,
    // then we'll start a new process. In the event that multiple yabridge
    // instances simultaneously try to start a new group process for the same
    // group, then the first process to listen on the socket will win and all
    // other processes will exit. When a plugin's host process has exited, it
    // will try to connect to the socket once more in the case that another
    // process is now listening on it.
    const fs::path endpoint_base_dir = sockets.base_dir_;
    const fs::path group_socket_path = generate_group_endpoint(
        *config.group, plugin_info.normalize_wine_prefix(),
        plugin_info.plugin_arch_);
    const auto connect = [&io_context, host_request, endpoint_base_dir,
                          group_socket_path]() {
        asio::local::stream_protocol::socket group_socket(io_context);
        group_socket.connect(group_socket_path.string());

        write_object(group_socket, host_request);
        const auto response = read_object<HostResponse>(group_socket);
        assert(response.pid > 0);
    };

    try {
        // Request an existing group host process to host our plugin
        connect();
    } catch (const std::system_error&) {
        // In case we could not connect to the socket, then we'll start a
        // new group host process. This process is detached immediately
        // because it should run independently of this yabridge instance as
        // it will likely outlive it.
        Process::Handle group_host =
            launch_host(host_path_, {group_socket_path.string()}, logger,
                        config, plugin_info);
        group_host.detach();

        group_host_connect_handler_ =
            std::jthread([this, connect, group_host = std::move(group_host)]() {
                set_realtime_priority(true);
                pthread_setname_np(pthread_self(), "group-connect");

                using namespace std::literals::chrono_literals;

                // We'll first try to connect to the group host we just spawned
                // TODO: Replace this polling with inotify
                while (group_host.running()) {
                    std::this_thread::sleep_for(20ms);

                    try {
                        connect();
                        return;
                    } catch (const std::system_error&) {
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
                } catch (const std::system_error&) {
                    startup_failed_ = true;
                }
            });
    }
}

fs::path GroupHost::path() {
    return host_path_;
}

bool GroupHost::running() noexcept {
    // When we are unable to connect to a new or existing group host process,
    // then we'll consider the startup failed and we'll allow the initialization
    // process to terminate.
    return !startup_failed_;
}

void GroupHost::terminate() {
    // There's no need to manually terminate group host processes as they will
    // shut down automatically after all plugins have exited. Manually closing
    // the sockets will cause the associated plugin to exit.
    sockets_.close();
}
