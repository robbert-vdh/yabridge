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

#include "process.h"

#include <cassert>

#include <spawn.h>
#include <sys/wait.h>

namespace fs = ghc::filesystem;

bool pid_running(pid_t pid) {
    // With regular individually hosted plugins we can simply check whether the
    // process is still running, however Boost.Process does not allow you to do
    // the same thing for a process that's not a direct child if this process.
    // When using plugin groups we'll have to manually check whether the PID
    // returned by the group host process is still active. We sadly can't use
    // `kill()` for this as that provides no way to distinguish between active
    // processes and zombies, and a terminated group host process will always be
    // left as a zombie process. If the process is active, then
    // `/proc/<pid>/{cwd,exe,root}` will be valid symlinks.
    std::error_code err;
    fs::canonical("/proc/" + std::to_string(pid) + "/exe", err);

    // NOTE: We can get a `EACCES` here if we don't have permissions to read
    //       this process's memory. This does mean that the process is still
    //       running.
    return !err || err.value() == EACCES;
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

bool Process::Handle::running() const noexcept {
    return pid_running(pid);
}

void Process::Handle::terminate() const noexcept {
    kill(pid, SIGINT);
    wait();
}

std::optional<int> Process::Handle::wait() const noexcept {
    int status = 0;
    assert(waitpid(pid, &status, 0) > 0);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else {
        return std::nullopt;
    }
}

Process::Process(std::string command) : command_(command) {}

Process::StringResult Process::spawn_get_stdout_line() const {
    /// We'll read the results from a pipe. The child writes to the second pipe,
    /// we'll read from the first one.
    int output_pipe[2];
    ::pipe(output_pipe);

    const auto argv = build_argv();
    const auto envp = env_ ? env_->make_environ() : environ;

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, output_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null",
                                     O_WRONLY | O_APPEND, 0);
    posix_spawn_file_actions_addclose(&actions, output_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, output_pipe[1]);

    pid_t child_pid = 0;
    const auto result = posix_spawnp(&child_pid, command_.c_str(), &actions,
                                     nullptr, argv, envp);

    close(output_pipe[1]);
    if (result == 2) {
        close(output_pipe[0]);
        return Process::CommandNotFound{};
    } else if (result != 0) {
        close(output_pipe[0]);
        return std::error_code(result, std::system_category());
    }

    // Try to read the first line out the output until the line feed
    std::array<char, 1024> output{0};
    FILE* output_pipe_stream = fdopen(output_pipe[0], "r");
    assert(output_pipe_stream);
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

char* const* Process::build_argv() const {
    argv_.clear();

    argv_.push_back(command_.c_str());
    for (const auto& arg : args_) {
        argv_.push_back(arg.c_str());
    }
    argv_.push_back(nullptr);

    return const_cast<char* const*>(argv_.data());
}
