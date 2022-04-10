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

#include <memory>
#include <optional>
#include <ostream>

#ifdef __WINE__
#include "../wine-host/asio-fix.h"
#endif

#include <asio/read_until.hpp>
#include <asio/streambuf.hpp>

// FIXME: Remove when we get rid of the patched_async_pipe
#include <asio/io_context.hpp>
#include <asio/posix/stream_descriptor.hpp>
#include <asio/post.hpp>

// FIXME: Get rid of Boost.Process and all of the wrangling below
#include <boost/process/async_pipe.hpp>
#include <boost/process/detail/posix/pipe_out.hpp>
#include <boost/process/posix.hpp>

#include "../utils.h"

/**
 * Boost 1.72 was released with a known breaking bug caused by a missing
 * typedef: https://github.com/boostorg/process/issues/116.
 *
 * Luckily this is easy to fix since it's not really possible to downgrade Boost
 * as it would break other applications.
 *
 * Check if this is still needed for other distros after Arch starts packaging
 * Boost 1.73.
 *
 * FIXME: This has been adopted to work with standalone Asio, we should replace
 *        this when we replace Boost.Process
 */
class patched_async_pipe {
    ::asio::posix::stream_descriptor _source;
    ::asio::posix::stream_descriptor _sink;

   public:
    typedef int native_handle_type;
    typedef ::asio::posix::stream_descriptor handle_type;
    typedef typename handle_type::executor_type executor_type;

    executor_type get_executor() { return _source.get_executor(); }

    inline patched_async_pipe(asio::io_context& ios)
        : patched_async_pipe(ios, ios) {}

    inline patched_async_pipe(asio::io_context& ios_source,
                              asio::io_context& ios_sink)
        : _source(ios_source), _sink(ios_sink) {
        int fds[2];
        if (::pipe(fds) == -1)
            boost::process::detail::throw_last_error("pipe(2) failed");

        _source.assign(fds[0]);
        _sink.assign(fds[1]);
    };

    inline patched_async_pipe(const patched_async_pipe& lhs);
    patched_async_pipe(patched_async_pipe&& lhs)
        : _source(std::move(lhs._source)), _sink(std::move(lhs._sink)) {
        lhs._source =
            ::asio::posix::stream_descriptor{lhs._source.get_executor()};
        lhs._sink = ::asio::posix::stream_descriptor{lhs._sink.get_executor()};
    }

    template <class CharT, class Traits = std::char_traits<CharT>>
    explicit patched_async_pipe(
        ::asio::io_context& ios_source,
        ::asio::io_context& ios_sink,
        const boost::process::detail::posix::basic_pipe<CharT, Traits>& p)
        : _source(ios_source, p.native_source()),
          _sink(ios_sink, p.native_sink()) {}

    template <class CharT, class Traits = std::char_traits<CharT>>
    explicit patched_async_pipe(
        asio::io_context& ios,
        const boost::process::detail::posix::basic_pipe<CharT, Traits>& p)
        : patched_async_pipe(ios, ios, p) {}

    template <class CharT, class Traits = std::char_traits<CharT>>
    inline patched_async_pipe& operator=(
        const boost::process::detail::posix::basic_pipe<CharT, Traits>& p);
    inline patched_async_pipe& operator=(const patched_async_pipe& rhs);

    inline patched_async_pipe& operator=(patched_async_pipe&& lhs);

    ~patched_async_pipe() {
        std::error_code ec;
        close(ec);
    }

    template <class CharT, class Traits = std::char_traits<CharT>>
    inline explicit
    operator boost::process::detail::posix::basic_pipe<CharT, Traits>() const;

    void cancel() {
        if (_sink.is_open())
            _sink.cancel();
        if (_source.is_open())
            _source.cancel();
    }

    void close() {
        if (_sink.is_open())
            _sink.close();
        if (_source.is_open())
            _source.close();
    }
    void close(std::error_code& ec) {
        if (_sink.is_open())
            _sink.close(ec);
        if (_source.is_open())
            _source.close(ec);
    }

    bool is_open() const { return _sink.is_open() || _source.is_open(); }
    void async_close() {
        if (_sink.is_open())
            asio::post(_sink.get_executor(), [this] { _sink.close(); });
        if (_source.is_open())
            asio::post(_source.get_executor(), [this] { _source.close(); });
    }

    template <typename MutableBufferSequence>
    std::size_t read_some(const MutableBufferSequence& buffers) {
        return _source.read_some(buffers);
    }
    template <typename MutableBufferSequence>
    std::size_t write_some(const MutableBufferSequence& buffers) {
        return _sink.write_some(buffers);
    }

    template <typename MutableBufferSequence>
    std::size_t read_some(const MutableBufferSequence& buffers,
                          std::error_code& ec) noexcept {
        return _source.read_some(buffers, ec);
    }
    template <typename MutableBufferSequence>
    std::size_t write_some(const MutableBufferSequence& buffers,
                           std::error_code& ec) noexcept {
        return _sink.write_some(buffers, ec);
    }

    native_handle_type native_source() const {
        return const_cast<asio::posix::stream_descriptor&>(_source)
            .native_handle();
    }
    native_handle_type native_sink() const {
        return const_cast<asio::posix::stream_descriptor&>(_sink)
            .native_handle();
    }

    template <typename MutableBufferSequence, typename ReadHandler>
    ASIO_INITFN_RESULT_TYPE(ReadHandler, void(std::error_code, std::size_t))
    async_read_some(const MutableBufferSequence& buffers,
                    ReadHandler&& handler) {
        return _source.async_read_some(buffers,
                                       std::forward<ReadHandler>(handler));
    }

    template <typename ConstBufferSequence, typename WriteHandler>
    ASIO_INITFN_RESULT_TYPE(WriteHandler, void(std::error_code, std::size_t))
    async_write_some(const ConstBufferSequence& buffers,
                     WriteHandler&& handler) {
        return _sink.async_write_some(buffers,
                                      std::forward<WriteHandler>(handler));
    }

    const handle_type& sink() const& { return _sink; }
    const handle_type& source() const& { return _source; }

    handle_type&& sink() && { return std::move(_sink); }
    handle_type&& source() && { return std::move(_source); }

    handle_type source(::asio::io_context& ios) && {
        ::asio::posix::stream_descriptor stolen(ios, _source.release());
        return stolen;
    }
    handle_type sink(::asio::io_context& ios) && {
        ::asio::posix::stream_descriptor stolen(ios, _sink.release());
        return stolen;
    }

    handle_type source(::asio::io_context& ios) const& {
        auto source_in = const_cast<::asio::posix::stream_descriptor&>(_source)
                             .native_handle();
        return ::asio::posix::stream_descriptor(ios, ::dup(source_in));
    }
    handle_type sink(::asio::io_context& ios) const& {
        auto sink_in = const_cast<::asio::posix::stream_descriptor&>(_sink)
                           .native_handle();
        return ::asio::posix::stream_descriptor(ios, ::dup(sink_in));
    }
};

// Even more of a mess, we can't use the nice `bp::std_out = ...`/`bp::std_err =
// ...` syntax anymore.
template <int p1, int p2>
struct patched_async_pipe_out
    : public boost::process::detail::posix::pipe_out<p1, p2> {
    patched_async_pipe& pipe;
    template <typename AsyncPipe>
    patched_async_pipe_out(AsyncPipe& p)
        : boost::process::detail::posix::pipe_out<p1, p2>(p.native_sink(),
                                                          p.native_source()),
          pipe(p) {}

    template <typename Pipe, typename Executor>
    static void close(Pipe& pipe, Executor&) {
        std::error_code ec;
        std::move(pipe).sink().close(ec);
    }

    template <typename Executor>
    void on_error(Executor& exec, const std::error_code&) {
        close(pipe, exec);
    }

    template <typename Executor>
    void on_success(Executor& exec) {
        close(pipe, exec);
    }
};

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
     * @param editor_tracing Whether we should enable debug tracing for the
     *   editor window handling. If we end up adding more of these options, we
     *   should move to a bitfield or something.
     * @param prefix An optional prefix for the logger. Useful for differentiate
     *   messages coming from the Wine VST host. Should end with a single space
     *   character.
     * @param prefix_timestamp Whether the log messages should be prefixed with
     *   a timestamp. The timestamp is added before `prefix`. This is set to
     *   `false` in `create_wine_stderr()` because otherwise you would end up
     *   with a second timestamp in the middle of the message (since all Wine
     *   output gets relayed through the logger using `async_log_pipe_lines()`).
     */
    Logger(std::shared_ptr<std::ostream> stream,
           Verbosity verbosity_level,
           bool editor_tracing,
           std::string prefix = "",
           bool prefix_timestamp = true);

    /**
     * Create a logger instance based on the set environment variables. See the
     * constants in `logging.cpp` for more information.
     *
     * @param prefix A message to prepend for every log message, useful to
     *   differentiate between the Wine process and the Linux VST plugin.
     * @param stream If specified, disregard `YABRIDGE_DEBUG_FILE` and output
     *   the log to this stream isntead.
     * @param prefix_timestamp Whether to prefix every log message with a
     *   timestamp.
     */
    static Logger create_from_environment(
        std::string prefix = "",
        std::shared_ptr<std::ostream> stream = nullptr,
        bool prefix_timestamp = true);

    /**
     * Create a special logger instance that outputs directly to STDERR without
     * any prefixes. This is used to be able to log filterable messages from the
     * Wine side of things.
     */
    static Logger create_wine_stderr();

    /**
     * Create a special logger instance for printing caught exceptions. This
     * simply calls `Logger::create_from_environment()` on the plugin side, and
     * `Logger::create_wine_stderr()` on the Wine side. Printing directly to
     * STDERR on the Wine side is fine, but on the plugin side that means that
     * we cannot redirect the output with `YABRIDGE_DEBUG_FILE`. So this should
     * also be used instead of writing to `std::cerr` when catching exceptions
     * in `src/common/`.
     */
    static Logger create_exception_logger();

    /**
     * Write a message to the log, prefixing it with a timestamp and this
     * logger's prefix string.
     *
     * @param message The message to write.
     */
    void log(const std::string& message);

    /**
     * Write output from an async pipe to the log on a line by line basis.
     * Useful for logging the Wine process's STDOUT and STDERR streams.
     *
     * @param pipe Some Asio stream that can be read from. Probably either
     *   `patched_async_pipe` or a stream descriptor.
     * @param buffer The buffer that will be used to read from `pipe`.
     * @param prefix Text to prepend to the line before writing to the log.
     */
    template <typename T>
    void async_log_pipe_lines(T& pipe,
                              asio::streambuf& buffer,
                              std::string prefix = "") {
        asio::async_read_until(
            pipe, buffer, '\n',
            [&, prefix](const std::error_code& error, size_t) {
                // When we get an error code then that likely means that the
                // pipe has been clsoed and we have reached the end of the file
                if (error) {
                    return;
                }

                std::string line;
                std::getline(std::istream(&buffer), line);
                log(prefix + line);

                async_log_pipe_lines(pipe, buffer, prefix);
            });
    }

    /**
     * Log a message that should only be printed when the `verbosity` is set to
     * `all_events`. This uses a lambda since producing a string always
     * allocates.
     *
     * @param message A lambda producing a string that should be written.
     */
    template <invocable_returning<std::string> F>
    void log_trace(F&& fn) {
        if (verbosity_ >= Verbosity::all_events) [[unlikely]] {
            log(fn());
        }
    }

    /**
     * Log a message that should only be printed when the `editor_tracing`
     * option is enabled. This can be useful to provide debugging information
     * for weird setup-specific bugs.
     *
     * @param message A lambda producing a string that should be written.
     */
    template <invocable_returning<std::string> F>
    void log_editor_trace(F&& fn) {
        if (editor_tracing_) [[unlikely]] {
            log(fn());
        }
    }

    /**
     * The verbosity level of this logger instance. Based on this certain
     * messages may or may not be shown.
     */
    const Verbosity verbosity_;

    /**
     * If this is set to true, then we'll print debug traces for the plugin
     * editor.
     */
    const bool editor_tracing_;

   private:
    /**
     * The output stream to write the log messages to. Typically either STDERR
     * or a file stream.
     */
    std::shared_ptr<std::ostream> stream_;

    /**
     * A prefix that gets prepended before every message.
     */
    const std::string prefix_;

    /**
     * Whether the log messages should be prefixed with a time stamp.
     */
    const bool prefix_timestamp_;
};
