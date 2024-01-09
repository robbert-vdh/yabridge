// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2024 Robbert van der Helm
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

#include <future>
#include <optional>
#include <type_traits>

#ifdef __WINE__
#include "../wine-host/use-linux-asio.h"
#endif
#include <asio/dispatch.hpp>
#include <asio/io_context.hpp>

/**
 * A helper to allow mutually recursive calling sequences with remote function
 * calls. Some plugins (and hosts) are very picky about which thread a function
 * call is coming from. This becomes an issue when the other side calls another
 * function in response to a function call, and when that other function _has_
 * to be handled on the same thread that called the first function. An example
 * of this is a VST3 plugin requesting a resize from the host. In response to
 * this the host will ask the plugin again for its current size, after which the
 * host will inform the plugin about its current size, and only then will the
 * original function call return. The issue here is that all of those function
 * calls have to be handled from both the plugin's and the host's GUI thread.
 * This helper lets you perform (potentially) mutually recursive function calls
 * where we'll spawn a new thread to do the blocking socket operations, and it
 * also lets you handle (potentially) mutually recursive function calls by
 * executing those on the original calling thread that initiated the mutually
 * recursive call sequence. For illustration, this looks like this:
 *
 * ```
 * thread 1: fork(fn)-\------------------/--foo()--\-----------/-
 * thread ?:           \   handle(foo)--/           \--...    /
 * thread 2:            \-----waiting for fn() to return-----/
 * ```
 *
 * Here `fork(fn)` will call the function `fn` on a new thread (which presumably
 * does some blocking socket operations), and `handle(foo)` will call `foo()` on
 * the thread that originally called `fork(fn)`. If the function passed to
 * `handle()` also calls `fork()` (or more likely, the function pass to
 * `handle()` calls an unmanaged plugin/host function that ends up performing a
 * mutually recursive callback), then this sequence allows for arbitrarily
 * nested mutual recursion.
 *
 * @tparam Thread The thread implementation to use. On the Linux side this
 *   should be `std::jthread` and on the Wine side this should be `Win32Thread`.
 */
template <typename Thread>
class MutualRecursionHelper {
   public:
    /**
     * Run `fn` from a new thread, during calls to `handle()` and
     * `maybe_handle()` on this thread. See the docstring on
     * `MutualRecursionHelper` for more information on this mechanism.
     *
     * @param fn A (blocking) function that should be called on another thread..
     *   This function will normally send a message to the other side using
     *   sockets, and it will then idly wait for a response.
     *
     * @return The return value of `fn`.
     */
    template <std::invocable F>
    std::invoke_result_t<F> fork(F&& fn) {
        using Result = std::invoke_result_t<F>;

        // This IO context will accept incoming calls from `handle()` and
        // `maybe_handle()` until the function returns. We keep these on a stack
        // as we need to support multiple levels of mutual recursion. This can
        // for instance happen during `IPlugView::attached() ->
        // IPlugFrame::resizeView() -> IPlugView::onSize()`.
        std::shared_ptr<asio::io_context> current_io_context =
            std::make_shared<asio::io_context>();
        {
            std::unique_lock lock(mutual_recursion_contexts_mutex_);
            mutual_recursion_contexts_.push_back(current_io_context);
        }

        // Instead of directly stopping the IO context, we'll reset this work
        // guard instead. This prevents us from accidentally cancelling any
        // outstanding tasks.
        auto work_guard = asio::make_work_guard(*current_io_context);

        // We will call the function from another thread so we can handle calls
        // to `handle()`/`maybe_handle()` from this thread
        std::promise<Result> response_promise{};
        Thread sending_thread([&]() {
            const Result response = fn();

            // Stop accepting additional work to be run from the calling thread
            // once `fn` returns (and we'll likely have gotten a response from
            // the other side). By resetting the work guard we do not cancel any
            // pending tasks, but `current_io_context->run()` will stop blocking
            // eventually.
            std::lock_guard lock(mutual_recursion_contexts_mutex_);
            work_guard.reset();
            mutual_recursion_contexts_.erase(std::find(
                mutual_recursion_contexts_.begin(),
                mutual_recursion_contexts_.end(), current_io_context));

            response_promise.set_value(response);
        });

        // Accept work from the other thread until we receive a response, at
        // which point the context will be stopped
        current_io_context->run();

        return response_promise.get_future().get();
    }

    /**
     * If another thread is currently calling `fork()`, then `fn` will be called
     * from that same thread. Otherwise `fn` will be called directly. See the
     * docstring on `MutualRecursionHelper`.
     *
     * @param fn The function to call on the mutual recursion thread, if that
     *   exists. This function may (indirectly) call `fork()` again to do nested
     *   mutual recursion.
     *
     * @return The result of `fn`, if it returns anything.
     *
     * @tparam F Some callable function that doesn't take any parameters.
     */
    template <std::invocable F>
    std::invoke_result_t<F> handle(F&& fn) {
        // If we're not currently engaged in some mutually recursive calling
        // sequence, then we'll execute the function on this thread
        if (const auto result = maybe_handle(std::forward<F>(fn))) {
            return *result;
        } else {
            return fn();
        }
    }

    /**
     * The same as `handle()`, but `fn` will only executed if we're currently
     * doing a mutually recursive function call through `fork()`. If no thread
     * is currently calling `fork()`, then this will return an `std::nullopt`
     * and `fn` won't be called and the caller must call `fn` itself.
     *
     * @see handle
     */
    template <std::invocable F>
    std::optional<std::invoke_result_t<F>> maybe_handle(F&& fn) {
        using Result = std::invoke_result_t<F>;

        std::unique_lock mutual_recursion_lock(
            mutual_recursion_contexts_mutex_);
        if (mutual_recursion_contexts_.empty()) {
            return std::nullopt;
        }

        // This function is only used in synchronous contexts, so we'll just
        // pretend that we're not doing any async things here
        std::packaged_task<Result()> do_call(std::forward<F>(fn));
        std::future<Result> do_call_response = do_call.get_future();
        asio::dispatch(*mutual_recursion_contexts_.back(), std::move(do_call));
        mutual_recursion_lock.unlock();

        return do_call_response.get();
    }

   private:
    /**
     * These IO contexts will let us call functions from the thread that's
     * currently calling `fork()` while we're waiting for the passed function to
     * return. We need an entire stack of these to be able to support deeply
     * nested mutual recursion, how fun! If `fork()` is being called multiple
     * times from the same thread (in a mutual recursion sequence), this stack
     * will contain multiple IO contexts. In that case the last context is the
     * active one. If the stack is empty, then there's currently no mutual
     * recursion going on.
     */
    std::vector<std::shared_ptr<asio::io_context>> mutual_recursion_contexts_;
    std::mutex mutual_recursion_contexts_mutex_;
};
