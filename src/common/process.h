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

#pragma once

#include <optional>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

#ifdef __WINE__
#include "../wine-host/asio-fix.h"
#endif

#include <unistd.h>
#include <asio/posix/stream_descriptor.hpp>
#include <ghc/filesystem.hpp>

// A minimal API akin to Boost.Process for launching and managing processes
// using plain Linux APIs. Needed so we can implement our chainloader without
// pulling in Boost.Process' Boost.Filesystem dependency (which would defeat the
// entire purpose).

/**
 * Check whether a process with the given PID is still active (and not a
 * zombie).
 */
bool pid_running(pid_t pid);

/**
 * Helper to create an `environ`-like environment object for passing to the
 * `exec*e()` family of functions.
 */
class ProcessEnvironment {
   public:
    /**
     * Create a new environment object based on an existing environment
     * described by an array of null-terminated strings, terminated by a null
     * pointer. You'll want to pass `environ` here.
     */
    ProcessEnvironment(char** initial_env);

    /**
     * Check if an environment variable exists within this environment. Mostly
     * useful for debugging.
     */
    bool contains(const std::string_view& key) const;

    /**
     * Get the value for an environment variable, if it exists in this
     * environment. Mostly useful for debugging.
     */
    std::optional<std::string_view> get(const std::string_view& key) const;

    /**
     * Add an environment variable to the environment or overwrite an
     * existing one.
     */
    void insert(const std::string& key, const std::string& value);

    /**
     * Create an environ-like object from the updated environment that can be
     * passed to the `exec*e()` functions. These pointers will be invalidated
     * when this object changes or when gets dropped.
     */
    char* const* make_environ() const;

   private:
    /**
     * All environment variables read from the constructor argument and those
     * inserted through `insert()`. These should be in `key=value` format.
     */
    std::vector<std::string> variables_;
    /**
     * Contains pointers to the strings in `variables`, so we can return a
     * `char**` in `make_environ()`.
     */
    mutable std::vector<char const*> recreated_environ_;
};

/**
 * A child process whose output can be captured. Simple wrapper around the Posix
 * APIs.
 */
class Process {
   public:
    /**
     * Marker to indicate that the program was not found.
     */
    struct CommandNotFound {};

    /**
     * A handle to a running process.
     */
    class Handle {
       public:
        /**
         * The process' ID.
         */
        const pid_t pid;

        /**
         * Whether the process is still running **and not a zombie**.
         */
        bool running() const noexcept;

        /**
         * Forcefully terminate the process by sending `SIGKILL`. Will reap the
         * process zombie after sending the signal.
         */
        void terminate() const noexcept;

        /**
         * Wait for the process to exit, returning the exit code if it exited
         * successfully. Returns a nullopt otherwise.
         */
        std::optional<int> wait() const noexcept;
    };

    using StringResult =
        std::variant<std::string, CommandNotFound, std::error_code>;
    using StatusResult = std::variant<int, CommandNotFound, std::error_code>;
    using HandleResult = std::variant<Handle, CommandNotFound, std::error_code>;

    /**
     * Build a process. Use the other functions to add arguments or to
     * launch the process.
     *
     * @param command The name of the command. `$PATH` will be searched for
     * this command if it is not absolute.
     */
    Process(std::string command);

    /**
     * Add an argument to the command invocation.
     */
    inline void arg(std::string arg) { args_.emplace_back(std::move(arg)); }

    /**
     * Use the specified environment for this command.
     *
     * @see environment
     */
    inline void environment(ProcessEnvironment env) { env_ = std::move(env); }

    /**
     * Spawn the process, leave STDIN, redirect STDERR to `/dev/null`, and
     * return the first line (without the trailing linefeed) of STDOUT. The
     * first output line will still be returned even if the process exits with a
     * non-zero exit code. Uses `posix_spawn()`, leaves file descriptors in
     * tact.
     */
    StringResult spawn_get_stdout_line();

   private:
    /**
     * Create the `argv` array from the command and the arguments. Only valid as
     * long as the pointers in `args_` at the time of calling stay valid.
     */
    char* const* build_argv() const;

    std::string command_;
    std::vector<std::string> args_;
    std::optional<ProcessEnvironment> env_;

    mutable std::vector<char const*> argv_;
};
