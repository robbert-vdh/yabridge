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

#include <memory>
#include <optional>
#include <ostream>

/**
 * Super basic logging facility meant for debugging malfunctioning VST
 * plugins. This is also used to redirect the output of the Wine process
 * because DAWs like Bitwig hide this from you, making it hard to debug
 * crashing plugins.
 *
 * @note This does not do any synchronisation. While this should technically
 *   be causing problems in concurrent use, writing strings to fstreams from
 *   multiple threads at the same time doesn't seem to produce corrupted text if
 *   you're writing an entire string at once even though the messages may be
 *   slightly out of order.
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
         * the plugin and the host. This excludes the `effEditIdle()` and
         * `audioMasterGetTime()` events and the event with opcode 52 since
         * those events are typically sent tens of times per second. Every
         * message is prefixed with a timestamp.
         */
        most_events = 1,
        /**
         * The same as the above but without filtering out any events. This is
         * very chatty but it can be crucial for debugging plugin-specific
         * problems.
         *
         * This will also print print information about the audio processing
         * callbacks, which can be useful for diagnosing misbehaving plugins.
         */
        all_events = 2,
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
    Logger(std::shared_ptr<std::ostream> stream,
           Verbosity verbosity_level,
           std::string prefix = "");

    /**
     * Create a logger instance based on the set environment variables. See the
     * constants in `logging.cpp` for more information.
     *
     * @param prefix A message to prepend for every log message, useful to
     *   differentiate between the Wine process and the Linux VST plugin.
     */
    static Logger create_from_environment(std::string prefix = "");

    /**
     * Create a special logger instance that outputs directly to STDERR without
     * any prefixes. This is used to be able to log filterable messages from the
     * Wine side of things.
     */
    static Logger create_wine_stderr();

    /**
     * Write a message to the log, prefixing it with a timestamp and this
     * logger's prefix string.
     *
     * @param message The message to write.
     */
    void log(const std::string& message);

    /**
     * Log a message that should only be printed when the `verbosity` is set to
     * `all_events`. This should only be used for simple primitive messages
     * without any formatting since the actual check happens within this
     * function.
     *
     * @param message The message to write.
     */
    void log_trace(const std::string& message);

    /**
     * The verbosity level of this logger instance. Based on this certain
     * messages may or may not be shown.
     */
    const Verbosity verbosity;

   private:
    /**
     * The output stream to write the log messages to. Typically either STDERR
     * or a file stream.
     */
    std::shared_ptr<std::ostream> stream;
    /**
     * A prefix that gets prepended before every message.
     */
    std::string prefix;
};
