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

#pragma once

#include <boost/process/environment.hpp>
#include <fstream>
#include <iostream>

/**
 * The environment variable indicating whether to log to a file. Will log to
 * STDERR if not specified.
 */
constexpr char logging_file_environment_variable[] = "YABRIDGE_DEBUG_FILE";
/**
 * The verbosity of the logging, defaults to `Logger::Verbosity::events` if
 * `logging_file_environment_variable` has been set and
 * `Logger::Verbosity::basic` otherwise.
 *
 * @see Logger::Verbosity
 */
constexpr char logging_verbosity_environment_variable[] =
    "YABRIDGE_DEBUG_VERBOSITY";

/**
 * Super basic logging facility meant for debugging malfunctioning VST
 * plugins. This is also used to redirect the output of the Wine process
 * because DAWs like Bitwig hide this from you, making it hard to debug
 * crashing plugins.
 */
class Logger {
   public:
    enum class Verbosity : int {
        /**
         * Only output basic information such as the VST plugin that's being
         * loaded and Wine's output. Doesn't add timestamps to reduce overhead.
         * To quiet down Wine you could optionally also set the `WINEDEBUG`
         * environment variable.
         */
        basic = 0,
        /**
         * Also print information about callbacks and functions being called by
         * the plugin and the host. Every message is prefixed with a timestamp.
         */
        events = 1,
        /**
         * Also print information about audio buffer processing. This can be
         * incredibly verbose and should only be used during development.
         */
        verbose = 2
    };

    /**
     * Initialize the logger with the following verbosity level.
     *
     * @param stream The `std::ostream` instance to use. Typically either a file
     *   stream or STDERR.
     * @param verbosity_level The verbosity of the logging, see the
     *   `Logger::Verbosity` constants above for a description of the verbosity
     *   levels.
     * @param prefix An optional prefix for the logger. Useful for differentiate
     *   messages coming from the Wine VST host.
     */
    Logger(std::ostream&& stream,
           Verbosity verbosity_level,
           std::string prefix = "")
        : stream(stream), verbosity(verbosity_level), prefix(prefix){};

    /**
     * Create a logger instance based on the set environment variables.
     *
     * @param prefix A message to prepend for every log message, useful to
     *   differentiate between the Wine process and the Linus VST plugin.
     */
    static Logger create_from_environment(std::string prefix = "") {
        auto env = boost::this_process::environment();
        std::string file_path = env.get(logging_file_environment_variable);
        std::string verbosity = env.get(logging_verbosity_environment_variable);

        // Default to `Verbosity::basic` if the environment variable has not
        // been set or if it is not an integer.
        Verbosity verbosity_level;
        try {
            verbosity_level = static_cast<Verbosity>(std::stoi(verbosity));
        } catch (const std::invalid_argument&) {
            verbosity_level = Verbosity::basic;
        }

        // If `file` points to a valid location then use create/truncate the
        // file and write all of the logs there, otherwise use STDERR
        std::ofstream log_file(file_path, std::fstream::out);
        if (log_file.is_open()) {
            return Logger(std::move(log_file), verbosity_level, prefix);
        } else {
            return Logger(std::move(std::cerr), verbosity_level, prefix);
        }
    }

    // TODO: Add dedicated logging functions for events and the Wine process's
    //       STDOUT and STDERR

   private:
    std::ostream& stream;
    Verbosity verbosity;
    std::string prefix;
};
