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

#include "group.h"

#include "../boost-fix.h"

#include <unistd.h>
#include <boost/process/environment.hpp>
#include <regex>

#include "../../common/communication/common.h"
#include "vst2.h"
#ifdef WITH_VST3
#include "vst3.h"
#endif

// FIXME: `std::filesystem` is broken in wineg++, at least under Wine 5.8. Any
//        path operation will thrown an encoding related error
namespace fs = boost::filesystem;

using namespace std::literals::chrono_literals;

/**
 * Listen on the specified endpoint if no process is already listening there,
 * otherwise throw. This is needed to handle these three situations:
 *
 * 1. The endpoint does not already exist, and we can simply create an endpoint.
 * 2. The endpoint already exists but it is stale and no process is currently
 *    listening. In this case we can remove the file and start listening.
 * 3. The endpoint already exists and another process is currently listening on
 *    it. In this situation we will throw immediately and we'll terminate this
 *    process.
 *
 * If anyone knows a better way to handle this, please let me know!
 *
 * @throw std::runtime_error If another process is already listening on the
 *        endpoint.
 */
boost::asio::local::stream_protocol::acceptor create_acceptor_if_inactive(
    boost::asio::io_context& io_context,
    boost::asio::local::stream_protocol::endpoint& endpoint);

/**
 * Create a logger prefix containing the group name based on the socket path.
 */
std::string create_logger_prefix(const fs::path& socket_path);

StdIoCapture::StdIoCapture(boost::asio::io_context& io_context,
                           int file_descriptor)
    : pipe(io_context),
      target_fd(file_descriptor),
      original_fd_copy(dup(file_descriptor)) {
    // We'll use the second element of these two file descriptors to reopen
    // `file_descriptor`, and the first one to read the captured contents from
    if (::pipe(pipe_fd) != 0) {
        throw std::system_error(errno, std::system_category());
    }

    // We've already created a copy of the original file descriptor, so we can
    // reopen it using the newly created pipe
    dup2(pipe_fd[1], target_fd);
    close(pipe_fd[1]);

    pipe.assign(pipe_fd[0]);
}

StdIoCapture::~StdIoCapture() noexcept {
    // Restore the original file descriptor and close the pipe. The other wend
    // was already closed in the constructor.
    dup2(original_fd_copy, target_fd);
    close(original_fd_copy);
    close(pipe_fd[0]);
}

GroupBridge::GroupBridge(boost::filesystem::path group_socket_path)
    : logger(Logger::create_from_environment(
          create_logger_prefix(group_socket_path))),
      main_context(),
      stdio_context(),
      stdout_redirect(stdio_context, STDOUT_FILENO),
      stderr_redirect(stdio_context, STDERR_FILENO),
      group_socket_endpoint(group_socket_path.string()),
      group_socket_acceptor(create_acceptor_if_inactive(main_context.context,
                                                        group_socket_endpoint)),
      shutdown_timer(main_context.context) {
    // Write this process's original STDOUT and STDERR streams to the logger
    logger.async_log_pipe_lines(stdout_redirect.pipe, stdout_buffer,
                                "[STDOUT] ");
    logger.async_log_pipe_lines(stderr_redirect.pipe, stderr_buffer,
                                "[STDERR] ");

    stdio_handler = Win32Thread([&]() {
        pthread_setname_np(pthread_self(), "group-stdio");

        stdio_context.run();
    });
}

GroupBridge::~GroupBridge() noexcept {
    // Our fancy `Vst2Sockets` and `Vst3Sockets` clean up after themselves, but
    // here we need to do it manually
    // TODO: Encapsulate this, destructors are evil
    fs::remove(group_socket_endpoint.path());

    stdio_context.stop();
}

bool GroupBridge::is_event_loop_inhibited() noexcept {
    std::lock_guard lock(active_plugins_mutex);

    for (auto& [parameters, value] : active_plugins) {
        auto& [thread, bridge] = value;
        if (bridge->inhibits_event_loop()) {
            return true;
        }
    }

    return false;
}

void GroupBridge::handle_plugin_run(size_t plugin_id, HostBridge* bridge) {
    // Blocks this thread until the plugin shuts down
    bridge->run();
    logger.log("'" + bridge->plugin_path.string() + "' has exited");

    // After the plugin has exited we'll remove this thread's plugin from the
    // active plugins. This is done within the IO context because the call to
    // `FreeLibrary()` has to be done from the main thread, or else we'll
    // potentially corrupt our heap. This way we can also properly join the
    // thread again. If no active plugins remain, then we'll terminate the
    // process.
    main_context.schedule_task([this, plugin_id]() {
        std::lock_guard lock(active_plugins_mutex);

        // The join is implicit because we're using Win32Thread (which mimics
        // std::jthread)
        active_plugins.erase(plugin_id);
    });

    // Defer actually shutting down the process to allow for fast plugin
    // scanning by allowing plugins to reuse the same group host process
    maybe_schedule_shutdown(4s);
}

void GroupBridge::handle_incoming_connections() {
    accept_requests();
    async_handle_events();

    // If we don't get a request to host a plugin within five seconds, we'll
    // shut the process down again.
    maybe_schedule_shutdown(5s);

    logger.log(
        "Group host is up and running, now accepting incoming connections");
    main_context.run();
}

void GroupBridge::accept_requests() {
    group_socket_acceptor.async_accept(
        [&](const boost::system::error_code& error,
            boost::asio::local::stream_protocol::socket socket) {
            std::lock_guard lock(active_plugins_mutex);

            // Stop the whole process when the socket gets closed unexpectedly
            if (error.failed()) {
                logger.log("Error while listening for incoming connections:");
                logger.log(error.message());

                main_context.stop();
            }

            // Read the parameters, and then host the plugin in this process
            // just like if we would be hosting the plugin individually through
            // `yabridge-hsot.exe`. We will reply with this process's PID so the
            // yabridge plugin will be able to tell if the plugin has caused
            // this process to crash during its initialization to prevent
            // waiting indefinitely on the sockets to be connected to.
            const auto request = read_object<HostRequest>(socket);
            write_object(socket, HostResponse{boost::this_process::get_id()});

            // The plugin has to be initiated on the IO context's thread because
            // this has to be done on the same thread that's handling messages,
            // and all window messages have to be handled from the same thread.
            logger.log("Received request to host " +
                       plugin_type_to_string(request.plugin_type) +
                       " plugin at '" + request.plugin_path +
                       "' using socket endpoint base directory '" +
                       request.endpoint_base_dir + "'");
            try {
                // Cancel the (initial) shutdown timer, since the plugin may
                // take longer to initialize if it is new
                shutdown_timer.cancel();

                std::unique_ptr<HostBridge> bridge = nullptr;
                switch (request.plugin_type) {
                    case PluginType::vst2:
                        bridge = std::make_unique<Vst2Bridge>(
                            main_context, request.plugin_path,
                            request.endpoint_base_dir, request.parent_pid);
                        break;
                    case PluginType::vst3:
#ifdef WITH_VST3
                        bridge = std::make_unique<Vst3Bridge>(
                            main_context, request.plugin_path,
                            request.endpoint_base_dir, request.parent_pid);
#else
                        throw std::runtime_error(
                            "This version of yabridge has not been compiled "
                            "with VST3 support");
#endif
                        break;
                    case PluginType::unknown:
                        throw std::runtime_error(
                            "Invalid plugin host request received, how did you "
                            "even manage to do this?");
                        break;
                }

                logger.log("Finished initializing '" + request.plugin_path +
                           "'");

                // Start listening for dispatcher events sent to the plugin's
                // socket on another thread. Parts of the actual event handling
                // will still be posted to this IO context so that any events
                // that potentially interact with the Win32 message loop are
                // handled from the main thread. We also pass a raw pointer to
                // the plugin so we don't have to immediately look the instance
                // up in the map again, as this would require us to immediately
                // lock the map again. This could otherwise result in a deadlock
                // when using the Spitfire plugins, as they will block the
                // message loop until `effOpen()` has been called and thus
                // prevent this lock from happening.
                const size_t plugin_id = next_plugin_id.fetch_add(1);
                active_plugins[plugin_id] = std::pair(
                    Win32Thread([this, plugin_id, plugin_ptr = bridge.get()]() {
                        const std::string thread_name =
                            "worker-" + std::to_string(plugin_id);
                        pthread_setname_np(pthread_self(), thread_name.c_str());

                        handle_plugin_run(plugin_id, plugin_ptr);
                    }),
                    std::move(bridge));
            } catch (const std::exception& error) {
                logger.log("Error while initializing '" + request.plugin_path +
                           "':");
                logger.log(error.what());

                maybe_schedule_shutdown(5s);
            }

            accept_requests();
        });
}

void GroupBridge::async_handle_events() {
    main_context.async_handle_events(
        [&]() {
            std::lock_guard lock(active_plugins_mutex);

            // Keep the loop responsive by not handling too many events at once.
            // All X11 events are handled from a Win32 timer so they'll still be
            // handled even when the GUI is blocked.
            //
            // For some reason the Melda plugins run into a seemingly infinite
            // timer loop for a little while after opening a second editor.
            // Without this limit everything will get blocked indefinitely. How
            // could this be fixed?
            HostBridge::handle_events();
        },
        [&]() { return !is_event_loop_inhibited(); });
}

boost::asio::local::stream_protocol::acceptor create_acceptor_if_inactive(
    boost::asio::io_context& io_context,
    boost::asio::local::stream_protocol::endpoint& endpoint) {
    // First try to listen on the endpoint normally
    try {
        return boost::asio::local::stream_protocol::acceptor(io_context,
                                                             endpoint);
    } catch (const boost::system::system_error&) {
        // If this failed, then either there is a stale socket file or another
        // process is already is already listening. In the last case we will
        // simply throw so the other process can handle the request.
        std::ifstream open_sockets("/proc/net/unix");
        const std::string endpoint_path = endpoint.path();
        for (std::string line; std::getline(open_sockets, line);) {
            if (line.size() < endpoint_path.size()) {
                continue;
            }

            std::string file = line.substr(line.size() - endpoint_path.size());
            if (file == endpoint_path) {
                // Another process is already listening, so we don't have to do
                // anything
                throw;
            }
        }

        // At this point we can remove the stale socket and start listening
        fs::remove(endpoint_path);
        return boost::asio::local::stream_protocol::acceptor(io_context,
                                                             endpoint);
    }
}

void GroupBridge::maybe_schedule_shutdown(
    std::chrono::steady_clock::duration delay) {
    std::lock_guard lock(shutdown_timer_mutex);

    shutdown_timer.expires_after(delay);
    shutdown_timer.async_wait([this](const boost::system::error_code& error) {
        // A previous timer gets canceled automatically when another plugin
        // exits
        if (error.failed()) {
            return;
        }

        std::lock_guard lock(active_plugins_mutex);
        if (active_plugins.size() == 0) {
            logger.log(
                "All plugins have exited, shutting down the group process");

            // The whole process will exit in `group-host.cpp` because of this
            main_context.stop();
        }
    });
}

std::string create_logger_prefix(const fs::path& socket_path) {
    // The group socket filename will be in the format
    // '/tmp/yabridge-group-<group_name>-<wine_prefix_id>-<architecture>.sock',
    // where Wine prefix ID is just Wine prefix ran through `std::hash` to
    // prevent collisions without needing complicated filenames. We want to
    // extract the group name.
    std::string socket_name =
        socket_path.filename().replace_extension().string();

    std::smatch group_match;
    std::regex group_regexp("^yabridge-group-(.*)-[^-]+-[^-]+$",
                            std::regex::ECMAScript);
    if (std::regex_match(socket_name, group_match, group_regexp)) {
        socket_name = group_match[1].str();

#ifdef __i386__
        // Mark 32-bit versions to avoid potential confusion caused by 32-bit
        // and regular 64-bit group processes with the same name running
        // alongside eachother
        socket_name += "-x32";
#endif
    }

    return "[" + socket_name + "] ";
}
