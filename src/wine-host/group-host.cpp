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

#include "boost-fix.h"

#include <unistd.h>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/filesystem.hpp>
#include <iostream>
#include <regex>
#include <thread>

// Generated inside of build directory
#include <src/common/config/config.h>
#include <src/common/config/version.h>

#include "wine-bridge.h"

// FIXME: `std::filesystem` is broken in wineg++, at least under Wine 5.8. Any
//        path operation will thrown an encoding related error.
namespace fs = boost::filesystem;

// TODO: Move most plumbing to another file

/**
 * Create a logger prefix containing the group name based on the socket path.
 */
std::string create_logger_prefix(const fs::path& socket_path);

/**
 * Continuously Read from a stream and write the lines to a logger instance.
 *
 * TODO: Merge this with the other similar function in `PluginBridge`
 */
void log_lines(Logger& logger,
               boost::asio::posix::stream_descriptor& pipe,
               boost::asio::streambuf& buffer,
               std::string prefix);

/**
 * This works very similar to the host application defined in
 * `individual-host.cpp`, but instead of just loading a single plugin this will
 * act as a daemon that can host multiple 'grouped' plugins. This works by
 * allowing the `libyabridge.so` instance to connect this this process over a
 * socket to ask this process to host a VST `.dll` file using a provided socket.
 * After that initialization step both the regular individual plugin host and
 * this group plugin host will function identically on both the plugin and the
 * Wine VST host side.
 *
 * The explicit calling convention is needed to work around a bug introduced in
 * Wine 5.7: https://bugs.winehq.org/show_bug.cgi?id=49138
 */
int __cdecl main(int argc, char* argv[]) {
    // Instead of directly hosting a plugin, this process will receive a UNIX
    // domain socket endpoint path that it should listen on to allow yabridge
    // instances to spawn plugins in this process.
    if (argc < 2) {
        std::cerr << "Usage: "
#ifdef __i386__
                  << yabridge_group_host_name_32bit
#else
                  << yabridge_group_host_name
#endif
                  << " <unix_domain_socket>" << std::endl;

        return 1;
    }

    const std::string group_socket_endpoint_path(argv[1]);

    // TODO: Before doing anything, try listening on the socket and fail
    //       silently (or log a message?) if another application is already
    //       listening on the socket. This way we don't need any complicated
    //       inter-process synchronization to ensure that there is a single
    //       active group host listening for this group.

    // Has to be initialized before redirecting the STDIO streams
    Logger logger = Logger::create_from_environment(
        create_logger_prefix(group_socket_endpoint_path));

    // Redirect this process's STDOUT and STDERR streams to a pipe so we can
    // process the output and redirect it to a logger. Needed to capture Wine
    // debug output, since this process will likely outlive the yabridge
    // instance that originally spawned it.
    int stdout_pipe[2];
    pipe(stdout_pipe);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    close(stdout_pipe[1]);

    int stderr_pipe[2];
    pipe(stderr_pipe);
    dup2(stderr_pipe[1], STDERR_FILENO);
    close(stderr_pipe[1]);

    boost::asio::io_context io_context;
    boost::asio::streambuf stdout_buffer;
    boost::asio::streambuf stderr_buffer;
    boost::asio::posix::stream_descriptor stdout_redirect(io_context,
                                                          stdout_pipe[0]);
    boost::asio::posix::stream_descriptor stderr_redirect(io_context,
                                                          stderr_pipe[0]);
    log_lines(logger, stdout_redirect, stdout_buffer, "[STDOUT] ");
    log_lines(logger, stderr_redirect, stderr_buffer, "[STDERR] ");
    std::thread io_handler([&]() { io_context.run(); });

    logger.log("Initializing yabridge group host version " +
               std::string(yabridge_git_version)
#ifdef __i386__
               + " (32-bit compatibility mode)"
#endif
    );

    // TODO: Remove debug prints
    printf("This should be caught now!\n");
    std::cerr << "This too!" << std::endl;

    // TODO: After initializing, listen for connections and spawn plugins
    //       the exact same way as what happens in `individual-host.cpp`
    // TODO: Allow this process to exit when the last plugin exits. Make sure
    //       that that doesn't cause any race conditions.

    // TODO: This usleep() is just to ensure that the second print to stderr
    //       also gets processed before stopping the IO context since we're
    //       immediately stopping it after starting. This would not needed in
    //       normal use.
    usleep(1000);
    io_context.stop();
    io_handler.join();
}

void log_lines(Logger& logger,
               boost::asio::posix::stream_descriptor& pipe,
               boost::asio::streambuf& buffer,
               std::string prefix) {
    boost::asio::async_read_until(pipe, buffer, '\n',
                                  [&, prefix](const auto&, size_t) {
                                      std::string line;
                                      std::getline(std::istream(&buffer), line);
                                      logger.log(prefix + line);

                                      log_lines(logger, pipe, buffer, prefix);
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
    }

    return "[" + socket_name + "] ";
}
