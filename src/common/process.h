// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2023 Robbert van der Helm
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

#include <ghc/filesystem.hpp>

// We also use this header from the chainloaders, and we don't want to pull in
// Asio there
#ifndef WITHOUT_ASIO
#ifdef __WINE__
#include "../wine-host/use-linux-asio.h"
#endif

#include <asio/posix/stream_descriptor.hpp>
#endif  // WITHOUT_ASIO

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
 * Return the search path as defined in `$PATH`, with `~/.local/share/yabridge`
 * appended to the end. Even though it likely won't be set, this does respect
 * `$XDG_DATA_HOME`. I'd rather not do this since more magic makes things harder
 * to comprehend, but I can understand that modifying your login shell's `PATH`
 * environment variable can be a big hurdle if you've never done anything like
 * that before. And since this is the recommended installation location, it
 * makes sense to also search there by default.
 */
std::vector<ghc::filesystem::path> get_augmented_search_path();

/**
 * Split a `PATH`-like environment variable on colons. These environment
 * variables don't support escaping, which makes this a lot simpler.
 */
std::vector<ghc::filesystem::path> split_path(const std::string_view& path_env);

/**
 * Search through a search path vector created by `split_path` for an executable
 * binary called `target`, returning the first match if any.
 */
std::optional<ghc::filesystem::path> search_in_path(
    const std::vector<ghc::filesystem::path>& path,
    const std::string_view& target);

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
 * APIs. The functions provided for running processes this way are very much
 * tailored towards yabridge's needs.
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
       protected:
        Handle(pid_t pid);

        friend Process;

       public:
        /**
         * Terminates the process when it gets dropped.
         */
        ~Handle();

        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;

        Handle(Handle&&) noexcept;
        Handle& operator=(Handle&&) noexcept;

        /**
         * The process' ID.
         */
        pid_t pid() const noexcept;

        /**
         * Whether the process is still running **and not a zombie**.
         */
        bool running() const noexcept;

        /**
         * Don't terminate the process when this object gets dropped.
         */
        void detach() noexcept;

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

       private:
        /**
         * If `true`, don't terminate the process when this object gets dropped.
         * Also set when this object gets moved from.
         */
        bool detached_ = false;

        pid_t pid_ = 0;
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
     * Add an argument to the command invocation. Returns a reference to this
     * object for easier chaining.
     */
    inline Process& arg(std::string arg) {
        args_.emplace_back(std::move(arg));
        return *this;
    }

    /**
     * Use the specified environment for this command.
     *
     * @see environment
     */
    inline Process& environment(ProcessEnvironment env) {
        env_ = std::move(env);
        return *this;
    }

    /**
     * Spawn the process, leave STDIN, redirect STDERR to `/dev/null`, and
     * return the first line (without the trailing linefeed) of STDOUT. The
     * first output line will still be returned even if the process exits with a
     * non-zero exit code. Uses `posix_spawn()`, leaves file descriptors in
     * tact.
     */
    StringResult spawn_get_stdout_line() const;

    /**
     * Spawn the process, leave STDOUT, STDIN and STDERR alone, and return an
     * empty string if the program ran successfully. Uses `posix_spawn()`,
     * leaves file descriptors in tact.
     */
    StatusResult spawn_get_status() const;

#ifndef WITHOUT_ASIO
    /**
     * Spawn the process without waiting for its completion, leave STDIN alone,
     * create pipes for STDOUT and STDERR, and assign those to the provided
     * (empty) stream descriptors. Use `posix_spawn()`, closes all non-STDIO
     * file descriptors. The process will be terminated when the child process
     * handle gets dropped.
     */
    HandleResult spawn_child_piped(
        asio::posix::stream_descriptor& stdout_pipe,
        asio::posix::stream_descriptor& stderr_pipe) const;
#endif  // WITHOUT_ASIO

    /**
     * Spawn the process without waiting for its completion, leave STDIN alone,
     * and redirect STDOUT and STDERR to a file. Use `posix_spawn()`, closes all
     * non-STDIO file descriptors. The process will be terminated when the child
     * process handle gets dropped.
     */
    HandleResult spawn_child_redirected(
        const ghc::filesystem::path& filename) const;

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
