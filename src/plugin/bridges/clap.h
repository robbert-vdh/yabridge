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

#pragma once

#include <shared_mutex>
#include <thread>

#include "../../common/communication/clap.h"
#include "../../common/logging/clap.h"
#include "../../common/mutual-recursion.h"
#include "clap-impls/plugin-factory-proxy.h"
#include "clap-impls/plugin-proxy.h"
#include "common.h"

/**
 * This handles the communication between the native host and a VST3 plugin
 * hosted in our Wine plugin host. This works in the same way as yabridge's VST3
 * bridgign. The `ClapPluginBridge` will be instantiated when the plugin first
 * gets loaded, and it will survive until the last instance of the plugin is
 * removed. The Wine host process will thus also have the same lifetime, and
 * even with yabridge's 'individual' plugin hosting other instances of the same
 * plugin will be handled by a single process.
 * The naming scheme of all of these 'bridge' classes is `<type>{,Plugin}Bridge`
 * for greppability reasons. The `Plugin` infix is added on the native plugin
 * side.
 */
class ClapPluginBridge : PluginBridge<ClapSockets<std::jthread>> {
   public:
    /**
     * Initializes the CLAP module by starting and setting up communicating with
     * the Wine plugin host.
     *
     * @param plugin_path The path to the **native** plugin library `.so` file.
     *   This is used to determine the path to the Windows plugin library we
     *   should load. For directly loaded bridges this should be
     *   `get_this_file_location()`. Chainloaded plugins should use the path of
     *   the chainloader copy instead.
     *
     * @throw std::runtime_error Thrown when the Wine plugin host could not be
     *   found, or if it could not locate and load a CLAP module.
     */
    explicit ClapPluginBridge(const ghc::filesystem::path& plugin_path);

    /**
     * Terminate the Wine plugin host process and drop all work when the module
     * gets unloaded.
     */
    ~ClapPluginBridge() noexcept;

    /**
     * The implementation for `clap_entry.get_factory`. When this is first
     * called, we'll query the factory's contents from the Wine plugin hosts if
     * the queried factory type is supported.
     *
     * @see plugin_factory_
     */
    const void* get_factory(const char* factory_id);

    /**
     * Fetch the plugin proxy instance along with a lock valid for the
     * instance's lifetime. This is mostly just to save some boilerplate
     * everywhere. Use C++17's structured binding as syntactic sugar to not have
     * to deal with the lock handle.
     */
    std::pair<clap_plugin_proxy&, std::shared_lock<std::shared_mutex>>
    get_proxy(size_t instance_id) noexcept;

    /**
     * Add a `clap_plugin_proxy` to the list of registered plugin proxies so we
     * can handle host callbacks. This function is called in
     * `clap_plugin_factory_proxy::create()`. This function also connects the
     * instance's audio thread socket.
     *
     * @param proxy_object The proxy object so we can access its host context
     *   and unique instance identifier.
     *
     * @see plugin_proxies_
     */
    void register_plugin_proxy(std::unique_ptr<clap_plugin_proxy> plugin_proxy);

    /**
     * Remove a previously registered `clap_plugin_proxy` from the list of
     * registered plugin proxies. Called in `clap_plugin_proxy::destroy()`after
     * asking the Wine plugin host to destroy the component on its side.
     *
     * @param instance_id The instance ID of the proxy that should be removed.
     *
     * @see plugin_proxies_
     */
    void unregister_plugin_proxy(size_t instance_id);

    /**
     * Send a control message to the Wine plugin host and return the response.
     * This is intended for main thread function calls, and it's a shorthand for
     * `sockets_.host_plugin_control_.send_message()` for use in CLAP interface
     * implementations.
     */
    template <typename T>
    typename T::Response send_main_thread_message(const T& object) {
        return sockets_.host_plugin_main_thread_control_.send_message(
            object, std::pair<ClapLogger&, bool>(logger_, true));
    }

    /**
     * Send a message to a plugin instance's audio thread. This is separate from
     * `send_message()`, which shares one socket for all plugin instances.
     */
    template <typename T>
    typename T::Response send_audio_thread_message(const T& object) {
        return sockets_.send_audio_thread_control_message(
            object, std::pair<ClapLogger&, bool>(logger_, true));
    }

    /**
     * Send an audio thread control message to a specific plugin instance,
     * receiving the results into an existing object. This is similar to the
     * `send_audio_thread_message()` above, but this lets us avoid
     allocations
     * in response objects that contain heap data.
     */
    template <typename T>
    typename T::Response& receive_audio_thread_message_into(
        const T& object,
        typename T::Response& response_object) {
        return sockets_.receive_audio_thread_control_message_into(
            object, response_object,
            std::pair<ClapLogger&, bool>(logger_, true));
    }

    // TODO: Do we need this for CLAP? If we do, update the docstring
    /**
     * Send a message meant to be executed on the main thread, and allow other
     * threads to call functions on _this thread_ while we're waiting for a
     * response. This lets us execute functions from the host's main thread
     * while it is also calling functions from that same thread. Because of
     * that, we also know that while this function is being called the host
     * won't be able to handle any `clap_host::request_callback()` requests. We
     * need this for a couple situations, like a plugin calling
     * `clap_host_*::rescan()` during state loading.
     *
     * We use the same trick in `ClapBridge`.
     */
    template <typename T>
    typename T::Response send_mutually_recursive_main_thread_message(
        const T& object) {
        return mutual_recursion_.fork(
            [&]() { return send_main_thread_message(object); });
    }

    /**
     * Run a callback on the host's GUI thread.
     *
     * If `send_mutually_recursive_main_thread_message()` is currently being
     * called, then run `fn` on the thread that's currently calling that
     * function and return the result of the call.
     *
     * Otherwise, use `clap_plugin_proxy::run_on_main_thread()` to use CLAP's
     * `clap_plugin::request_callback()` mechanic.
     *
     * @return The result of calling `fn`
     *
     * @see clap_plugin_proxy::run_on_main_thread
     */
    template <std::invocable F>
    std::future<std::invoke_result_t<F>> run_on_main_thread(
        clap_plugin_proxy& plugin,
        F&& fn) {
        using Result = std::invoke_result_t<F>;

        // If `ClapBridge::send_mutually_recursive_main_thread_message()` is
        // currently being called, then we'll call `fn` from that same thread.
        // Otherwise we'll schedule the task to be run using the host's main
        // thread using `clap_host::request_callback()`. This is needed because
        // `request_callback()` won't do anything if that thread is currently
        // blocked.

        // Modifying the `mutual_recursion_` methods to handle `void` correctly
        // would lead to a lot more template soup, so we'll just work around it
        // here.
        // TODO: At some point, do improve the API so it can handle void without
        //       workaorunds
        if constexpr (std::is_void_v<Result>) {
            if (const auto result =
                    mutual_recursion_.maybe_handle([f = std::forward<F>(fn)]() {
                        f();
                        return Ack{};
                    })) {
                // Apparently there's no way to just create a ready future
                std::promise<void> result_promise;
                result_promise.set_value();

                return result_promise.get_future();
            }
        } else {
            if (const auto result =
                    mutual_recursion_.maybe_handle(std::forward<F>(fn))) {
                std::promise<Result> result_promise;
                result_promise.set_value(std::move(*result));

                return result_promise.get_future();
            }
        }

        return plugin.run_on_main_thread(std::forward<F>(fn));
    }

    /**
     * The logging facility used for this instance of yabridge. Wraps around
     * `PluginBridge::generic_logger`.
     */
    ClapLogger logger_;

   private:
    /**
     * Handles callbacks from the plugin to the host over the
     * `plugin_host_callback_` sockets.
     */
    std::jthread host_callback_handler_;

    /**
     * Our plugin factory, containing information about all plugins supported by
     * the bridged CLAP plugin's factory. This is initialized the first time the
     * host tries to query this in `clap_entry->get_factory()`.
     *
     * @related get_factory
     */
    std::unique_ptr<clap_plugin_factory_proxy> plugin_factory_;

    /**
     * Proxies for all CLAP plugin instances we created for this plugin library.
     * These are all keyed by an ID created on the Wine side when initializing
     * the plugin. That lets us send function calls from the host to the correct
     * plugin instance, and callbacks from a plugin instance to the correct host
     * instance. Instances are added here through a call by
     * `register_plugin_proxy()` in `clap_plugin_factory_proxy::create()`, and
     * they are removed again by a call to `unregister_plugin_proxy()` in
     * `clap_plugin_proxy::destroy()`.
     */
    std::unordered_map<size_t, std::unique_ptr<clap_plugin_proxy>>
        plugin_proxies_;

    /**
     * In theory all object handling is safe iff the host also doesn't do
     * anything weird even without locks, but we'll still prevent adding or
     * removing instances while accessing other instances at the same time
     * anyways. See `ClapBridge::plugin_instances_mutex_` for more details.
     *
     * TODO: At some point replace this with a multiple reader single writer
     *       lock based by a spinlock. Because this lock is rarely contested
     *       `get_proxy()` never yields to the scheduler during audio
     *       processing, but it's still something we should avoid at all costs.
     */
    std::shared_mutex plugin_proxies_mutex_;

    /**
     * Used in `ClapBridge::send_mutually_recursive_message()` to be able to
     * execute functions from that same calling thread while we're waiting for a
     * response. See the uses for `send_mutually_recursive_message()` for use
     * cases where this is needed.
     */
    MutualRecursionHelper<std::jthread> mutual_recursion_;
};
