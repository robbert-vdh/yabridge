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

#include "group.h"

#include <unistd.h>
#include <boost/asio/read_until.hpp>
#include <regex>

// FIXME: `std::filesystem` is broken in wineg++, at least under Wine 5.8. Any
//        path operation will thrown an encoding related error
namespace fs = boost::filesystem;

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
    ::pipe(pipe_fd);

    // We've already created a copy of the original file descriptor, so we can
    // reopen it using the newly created pipe
    dup2(pipe_fd[1], target_fd);
    close(pipe_fd[1]);

    pipe.assign(pipe_fd[0]);
}

StdIoCapture::~StdIoCapture() {
    // Restore the original file descriptor and close the pipe. The other wend
    // was already closed in the constructor.
    dup2(original_fd_copy, target_fd);
    close(original_fd_copy);
    close(pipe_fd[0]);
}

bool PluginParameters::operator==(const PluginParameters& other) const {
    return removeme == other.removeme;
}

GroupBridge::GroupBridge(boost::filesystem::path group_socket_path)
    : logger(Logger::create_from_environment(
          create_logger_prefix(group_socket_path))),
      io_context(),
      stdout_redirect(io_context, STDOUT_FILENO),
      stderr_redirect(io_context, STDERR_FILENO),
      group_socket_endpoint(group_socket_path.string()),
      group_socket_acceptor(io_context, group_socket_endpoint) {
    // TODO: After initializing, listen for connections and spawn plugins
    //       the exact same way as what happens in `individual-host.cpp`
    // TODO: Allow this process to exit when the last plugin exits. Make sure
    //       that that doesn't cause any race conditions.

    // Write this process's original STDOUT and STDERR streams to the logger
    async_log_pipe_lines(stdout_redirect.pipe, stdout_buffer, "[STDOUT] ");
    async_log_pipe_lines(stderr_redirect.pipe, stderr_buffer, "[STDERR] ");
}

void GroupBridge::handle_host_plugin(const PluginParameters& parameters) {
    // TODO: Start the plugin,
    // TODO: Allow this process to exit when the last plugin exits. Make sure
    //       that that doesn't cause any race conditions.
}

void GroupBridge::handle_incoming_connections() {
    // TODO: Accept connections here

    logger.log("Now accepting incoming connections");
    io_context.run();
}

void GroupBridge::async_log_pipe_lines(
    boost::asio::posix::stream_descriptor& pipe,
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
