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

#include "process.h"

#include <cassert>

#include <spawn.h>
#include <sys/wait.h>

namespace fs = ghc::filesystem;

bool pid_running(pid_t pid) {
    // In theory you could `kill(0)` a process to check if it's still active,
    // but that doesn't distinguish between actually running processes and
    // unreaped zombies, and terminated group host processes will always be left
    // as zombies since there may not be anything left to reap them. Instead
    // we'll check whether one of `/proc/<pid>/{cwd,exe,root}` are valid
    // symlinks.
    std::error_code err;
    fs::canonical("/proc/" + std::to_string(pid) + "/exe", err);

    // NOTE: We can get a `EACCES` here if we don't have permissions to read
    //       this process's memory. This does mean that the process is still
    //       running.
    return !err || err.value() == EACCES;
}

std::vector<ghc::filesystem::path> split_path(
    const std::string_view& path_env) {
    // C++ has these great split range adapters. That are completely usless.
    std::vector<fs::path> search_path;

    size_t segment_begin = 0;
    while (segment_begin != path_env.size()) {
        const size_t segment_end = path_env.find(':', segment_begin);
        if (segment_end == std::string_view::npos) {
            search_path.push_back(path_env.substr(
                segment_begin, path_env.size() - segment_begin));
            break;
        } else {
            search_path.push_back(
                path_env.substr(segment_begin, segment_end - segment_begin));

            // Restart after the colon
            segment_begin = segment_end + 1;
        }
    }

    return search_path;
}

std::optional<ghc::filesystem::path> search_in_path(
    const std::vector<ghc::filesystem::path>& path,
    const std::string_view& target) {
    for (const auto& dir : path) {
        ghc::filesystem::path candidate = dir / target;
        if (access(candidate.c_str(), X_OK) == 0) {
            return candidate;
        }
    }

    return std::nullopt;
}

ProcessEnvironment::ProcessEnvironment(char** initial_env) {
    // We'll need to read all strings from `initial_env`. They _should_ all be
    // zero-terminated strings, with a null pointer to indicate the end of the
    // array.
    assert(initial_env);
    while (*initial_env) {
        variables_.push_back(*initial_env);
        initial_env++;
    }
}

bool ProcessEnvironment::contains(const std::string_view& key) const {
    for (const auto& variable : variables_) {
        if (variable.starts_with(key) && variable.size() > key.size() &&
            variable[key.size()] == '=') {
            return true;
        }
    }

    return false;
}

std::optional<std::string_view> ProcessEnvironment::get(
    const std::string_view& key) const {
    for (const auto& variable : variables_) {
        if (variable.starts_with(key) && variable.size() > key.size() &&
            variable[key.size()] == '=') {
            return std::string_view(variable).substr(key.size() + 1);
        }
    }

    return std::nullopt;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void ProcessEnvironment::insert(const std::string& key,
                                const std::string& value) {
    variables_.push_back(key + "=" + value);
}

char* const* ProcessEnvironment::make_environ() const {
    recreated_environ_.clear();

    for (const auto& variable : variables_) {
        recreated_environ_.push_back(variable.c_str());
    }
    recreated_environ_.push_back(nullptr);

    return const_cast<char* const*>(recreated_environ_.data());
}

Process::Handle::Handle(pid_t pid) : pid_(pid) {}

Process::Handle::~Handle() {
    if (!detached_) {
        // If this function has already been called then that's okay
        terminate();
    }
}

Process::Handle::Handle(Handle&& o) noexcept : pid_(o.pid_) {
    o.detached_ = true;
}

Process::Handle& Process::Handle::operator=(Handle&& o) noexcept {
    o.detached_ = true;

    pid_ = o.pid_;

    return *this;
}

pid_t Process::Handle::pid() const noexcept {
    return pid_;
}

bool Process::Handle::running() const noexcept {
    return pid_running(pid_);
}

void Process::Handle::detach() noexcept {
    detached_ = true;
}

void Process::Handle::terminate() const noexcept {
    kill(pid_, SIGINT);
    wait();
}

std::optional<int> Process::Handle::wait() const noexcept {
    // This may fail if we've already reaped the process and terminate gets
    // called another time, so we won't check the result here
    int status = 0;
    waitpid(pid_, &status, 0);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else {
        return std::nullopt;
    }
}

Process::Process(std::string command) : command_(command) {}

Process::StringResult Process::spawn_get_stdout_line() const {
    // We'll read the results from a pipe. The child writes to the second pipe,
    // we'll read from the first one.
    int stdout_pipe_fds[2];
    assert(pipe(stdout_pipe_fds) == 0);

    const auto argv = build_argv();
    const auto envp = env_ ? env_->make_environ() : environ;

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, stdout_pipe_fds[1],
                                     STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null",
                                     O_WRONLY | O_APPEND, 0);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe_fds[0]);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe_fds[1]);

    pid_t child_pid = 0;
    const auto result = posix_spawnp(&child_pid, command_.c_str(), &actions,
                                     nullptr, argv, envp);

    close(stdout_pipe_fds[1]);
    if (result == 2) {
        close(stdout_pipe_fds[0]);
        return Process::CommandNotFound{};
    } else if (result != 0) {
        close(stdout_pipe_fds[0]);
        return std::error_code(result, std::system_category());
    }

    // Try to read the first line out the output until the line feed
    std::array<char, 1024> output{0};
    FILE* output_pipe_stream = fdopen(stdout_pipe_fds[0], "r");
    assert(output_pipe_stream);
    [[maybe_unused]] char* not_relevant =
        fgets(output.data(), output.size(), output_pipe_stream);
    fclose(output_pipe_stream);

    int status = 0;
    assert(waitpid(child_pid, &status, 0) > 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) == 127) {
        return Process::CommandNotFound{};
    } else {
        // `fgets()` returns the line feed, so we'll get rid of that
        std::string output_str(output.data());
        if (output_str.back() == '\n') {
            output_str.pop_back();
        }

        return output_str;
    }
}

Process::StatusResult Process::spawn_get_status() const {
    const auto argv = build_argv();
    const auto envp = env_ ? env_->make_environ() : environ;

    pid_t child_pid = 0;
    const auto result = posix_spawnp(&child_pid, command_.c_str(), nullptr,
                                     nullptr, argv, envp);
    if (result == 2) {
        return Process::CommandNotFound{};
    } else if (result != 0) {
        return std::error_code(result, std::system_category());
    }

    int status = 0;
    assert(waitpid(child_pid, &status, 0) > 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) == 127) {
        return Process::CommandNotFound{};
    } else {
        return WEXITSTATUS(status);
    }
}

Process::HandleResult Process::spawn_child_piped(
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    asio::posix::stream_descriptor& stdout_pipe,
    asio::posix::stream_descriptor& stderr_pipe) const {
    // We'll reopen the child process' STDOUT and STDERR stream from a pipe, and
    // we'll assign the other ends of those pipes to the stream descriptors
    // passed to this function so they can be read from asynchronously in an
    // Asio IO context loop. We'll read from the first elements of these pipes,
    // and the child process will write to the second elements.
    int stdout_pipe_fds[2];
    int stderr_pipe_fds[2];
    assert(pipe(stdout_pipe_fds) == 0);
    assert(pipe(stderr_pipe_fds) == 0);

    const auto argv = build_argv();
    const auto envp = env_ ? env_->make_environ() : environ;

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, stdout_pipe_fds[1],
                                     STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stderr_pipe_fds[1],
                                     STDERR_FILENO);
    // We'll close the four pipe fds along with the rest of the file descriptors

// NOTE: If the Wine process outlives the host, then it may cause issues if
//       our process is still keeping the host's file descriptors alive
//       that. This can prevent Ardour from restarting after an unexpected
//       shutdown. Because of this we won't use `vfork()`, but instead we'll
//       just manually close all non-STDIO file descriptors.
#if (__GLIBC__ > 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34)
    posix_spawn_file_actions_addclosefrom_np(&actions, STDERR_FILENO + 1);
#else
    const int max_fds = static_cast<int>(sysconf(_SC_OPEN_MAX));
    for (int fd = STDERR_FILENO + 1; fd < max_fds; fd++) {
        posix_spawn_file_actions_addclose(&actions, fd);
    }
#endif

    pid_t child_pid = 0;
    const auto result = posix_spawnp(&child_pid, command_.c_str(), &actions,
                                     nullptr, argv, envp);

    // We'll assign the read ends of the pipes to the Asio stream descriptors
    // passed to this function, even if launching the process failed.
    // `asio::posix::stream_descriptor::assign()` will take ownership of the FD
    // and close it when the object gets dropped.
    stdout_pipe.assign(stdout_pipe_fds[0]);
    stderr_pipe.assign(stderr_pipe_fds[0]);
    close(stdout_pipe_fds[1]);
    close(stderr_pipe_fds[1]);

    if (result == 2) {
        return Process::CommandNotFound{};
    } else if (result != 0) {
        return std::error_code(result, std::system_category());
    }

    // With glibc `posix_spawn*()` will return 2/`ENOENT` when the file does not
    // exist, but the specification says that it should return a PID that exits
    // with status 127 instead. I have no idea how we'd check for that without
    // waiting here though, so this check may not work
    int status = 0;
    assert(waitpid(child_pid, &status, WNOHANG) >= 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
        return Process::CommandNotFound{};
    } else {
        return Handle(child_pid);
    }
}

Process::HandleResult Process::spawn_child_redirected(
    const ghc::filesystem::path& filename) const {
    const auto argv = build_argv();
    const auto envp = env_ ? env_->make_environ() : environ;

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, filename.c_str(),
                                     O_WRONLY | O_CREAT | O_APPEND, 0640);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, filename.c_str(),
                                     O_WRONLY | O_CREAT | O_APPEND, 0640);

    // See the note in the other function
#if (__GLIBC__ > 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34)
    posix_spawn_file_actions_addclosefrom_np(&actions, STDERR_FILENO + 1);
#else
    const int max_fds = static_cast<int>(sysconf(_SC_OPEN_MAX));
    for (int fd = STDERR_FILENO + 1; fd < max_fds; fd++) {
        posix_spawn_file_actions_addclose(&actions, fd);
    }
#endif

    pid_t child_pid = 0;
    const auto result = posix_spawnp(&child_pid, command_.c_str(), &actions,
                                     nullptr, argv, envp);
    if (result == 2) {
        return Process::CommandNotFound{};
    } else if (result != 0) {
        return std::error_code(result, std::system_category());
    }

    int status = 0;
    assert(waitpid(child_pid, &status, WNOHANG) >= 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
        return Process::CommandNotFound{};
    } else {
        return Handle(child_pid);
    }
}

char* const* Process::build_argv() const {
    argv_.clear();

    argv_.push_back(command_.c_str());
    for (const auto& arg : args_) {
        argv_.push_back(arg.c_str());
    }
    argv_.push_back(nullptr);

    return const_cast<char* const*>(argv_.data());
}
