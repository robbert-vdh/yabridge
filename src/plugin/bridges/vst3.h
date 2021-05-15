// yabridge: a Wine VST bridge
// Copyright (C) 2020-2021 Robbert van der Helm
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

#include <thread>

#include "../../common/communication/vst3.h"
#include "../../common/logging/vst3.h"
#include "common.h"
#include "vst3-impls/plugin-factory-proxy.h"

#include <boost/asio/dispatch.hpp>

// Forward declarations
class Vst3PluginProxyImpl;

/**
 * This handles the communication between the native host and a VST3 plugin
 * hosted in our Wine plugin host. VST3 is handled very differently from VST2
 * because a plugin is no longer its own entity, but rather a definition of
 * objects that the host can create and interconnect. This `Vst3PluginBridge`
 * will be instantiated when the plugin first gets loaded, and it will survive
 * until the last instance of the plugin gets removed. The Wine host process
 * will thus also have the same lifetime, and even with yabridge's 'individual'
 * plugin hosting other instances of the same plugin will be handled by a single
 * process.
 *
 * @remark See the comments at the top of `vst3-plugin.cpp` for more
 *   information.
 *
 * The naming scheme of all of these 'bridge' classes is `<type>{,Plugin}Bridge`
 * for greppability reasons. The `Plugin` infix is added on the native plugin
 * side.
 */
class Vst3PluginBridge : PluginBridge<Vst3Sockets<std::jthread>> {
   public:
    /**
     * Initializes the VST3 module by starting and setting up communicating with
     * the Wine plugin host.
     *
     * @throw std::runtime_error Thrown when the Wine plugin host could not be
     *   found, or if it could not locate and load a VST3 module.
     */
    explicit Vst3PluginBridge();

    /**
     * Terminate the Wine plugin host process and drop all work when the module
     * gets unloaded.
     */
    ~Vst3PluginBridge() noexcept;

    /**
     * When the host loads the module it will call `GetPluginFactory()` which
     * will in turn call this function. The idea is that we return an
     * `IPluginFactory*` that acts as an owned `IPtr<IPluginFactory>`, so we
     * have to manually increase the reference count here for every plugin
     * factory instance we return.
     *
     * @see plugin_factory
     */
    Steinberg::IPluginFactory* get_plugin_factory();

    /**
     * Add a `Vst3PluginProxyImpl` to the list of registered proxy objects so we
     * can handle host callbacks. This function is called in
     * `Vst3PluginProxyImpl`'s constructor. If the plugin supports the
     * `IAudioProcessor` or `IComponent` interfaces, then we'll also connect to
     * a dedicated audio processing socket.
     *
     * @param proxy_object The proxy object so we can access its host context
     *   and unique instance identifier.
     *
     * @see plugin_proxies
     */
    void register_plugin_proxy(Vst3PluginProxyImpl& proxy_object);

    /**
     * Remove a previously registered `Vst3PluginProxyImpl` from the list of
     * registered proxy objects. Called during the object's destructor after
     * asking the Wine plugin host to destroy the component on its side.
     *
     * @param proxy_object The proxy object so we can access its unique instance
     *   identifier.
     *
     * @see plugin_proxies
     */
    void unregister_plugin_proxy(Vst3PluginProxyImpl& proxy_object);

    /**
     * Send a control message to the Wine plugin host return the response. This
     * is a shorthand for `sockets.host_vst_control.send_message` for use in
     * VST3 interface implementations.
     */
    template <typename T>
    typename T::Response send_message(const T& object) {
        return sockets.host_vst_control.send_message(
            object, std::pair<Vst3Logger&, bool>(logger, true));
    }

    /**
     * Send an `IAudioProcessor` or `IComponent` control message to a specific
     * plugin instance. This is separated from the above `send_message()` for
     * performance reasons, as this way every instance has its own dedicated
     * socket and thread.
     */
    template <typename T>
    typename T::Response send_audio_processor_message(const T& object) {
        return sockets.send_audio_processor_message(
            object, std::pair<Vst3Logger&, bool>(logger, true));
    }

    /**
     * Send an `IAudioProcessor` or `IComponent` control message to a specific
     * plugin instance, receiving the results into an existing object. This is
     * similar to the `send_audio_processor_message()` above, but this lets us
     * avoid allocations in response objects that contain heap data.
     */
    template <typename T>
    typename T::Response& receive_audio_processor_message_into(
        const T& object,
        typename T::Response& response_object) {
        return sockets.receive_audio_processor_message_into(
            object, response_object,
            std::pair<Vst3Logger&, bool>(logger, true));
    }

    /**
     * Send a message, and allow other threads to call functions on _this
     * thread_ while we're waiting for a response. This lets us execute
     * functions from the host's GUI thread while it is also calling functions
     * from that same thread. Because of that, we also know that while this
     * function is being called the host won't be able to handle any `IRunLoop`
     * events. We need this to support REAPER, because REAPER requires function
     * calls involving the GUI to be run from the GUI thread. Grep for
     * `run_gui_task` for instances of this.
     *
     * We use the same trick in `Vst3Bridge`.
     */
    template <typename T>
    typename T::Response send_mutually_recursive_message(const T& object) {
        using TResponse = typename T::Response;

        // This IO context will accept incoming calls from `run_gui_task()`
        // until we receive a response. We keep these on a stack as we need to
        // support multiple levels of mutual recursion. This could happen during
        // `IPlugView::attached() -> IPlugFrame::resizeView() ->
        // IPlugView::onSize()`.
        std::shared_ptr<boost::asio::io_context> current_io_context =
            std::make_shared<boost::asio::io_context>();
        {
            std::unique_lock lock(mutual_recursion_contexts_mutex);
            mutual_recursion_contexts.push_back(current_io_context);
        }

        // Instead of directly stopping the IO context, we'll reset this work
        // guard instead. This prevents us from accidentally cancelling any
        // outstanding tasks.
        auto work_guard = boost::asio::make_work_guard(*current_io_context);

        // We will call the function from another thread so we can handle calls
        // to from this thread
        std::promise<TResponse> response_promise{};
        std::jthread sending_thread([&]() {
            set_realtime_priority(true);

            const TResponse response = send_message(object);

            // Stop accepting additional work to be run from the calling thread
            // once we receive a response. By resetting the work guard we do not
            // cancel any pending tasks, but `current_io_context->run()` will
            // stop blocking eventually.
            std::lock_guard lock(mutual_recursion_contexts_mutex);
            work_guard.reset();
            mutual_recursion_contexts.erase(
                std::find(mutual_recursion_contexts.begin(),
                          mutual_recursion_contexts.end(), current_io_context));

            response_promise.set_value(response);
        });

        // Accept work from the other thread until we receive a response, at
        // which point the context will be stopped
        current_io_context->run();

        return response_promise.get_future().get();
    }

    /**
     * If `send_mutually_recursive_message()` is currently being called, then
     * run `cb` on the thread that's currently calling that function. If there's
     * currently no mutually recursive function call going on, this will return
     * false, and the caller should call `cb` itself.
     *
     * @return Whether `cb` was scheduled to run on the mutual recursion thread.
     *
     * @see Vst3PlugViewProxyImpl::run_gui_task
     */
    template <typename F>
    bool maybe_run_on_mutual_recursion_thread(F& cb) {
        // We're handling an `F&` here because we cannot copy a
        // `packged_task()`, and we need to be able to move that actual task
        std::unique_lock mutual_recursion_lock(mutual_recursion_contexts_mutex);
        if (!mutual_recursion_contexts.empty()) {
            boost::asio::dispatch(*mutual_recursion_contexts.back(),
                                  std::move(cb));
            return true;
        } else {
            return false;
        }
    }

    /**
     * The logging facility used for this instance of yabridge. Wraps around
     * `PluginBridge::generic_logger`.
     */
    Vst3Logger logger;

   private:
    /**
     * Handles callbacks from the plugin to the host over the
     * `vst_host_callback` sockets.
     */
    std::jthread host_callback_handler;

    /**
     * Our plugin factory. All information about the plugin and its supported
     * classes are copied directly from the Windows VST3 plugin's factory on the
     * Wine side, and we'll provide an implementation that can send control
     * messages to the Wine plugin host.
     *
     * @related get_plugin_factory
     */
    Steinberg::IPtr<Vst3PluginFactoryProxyImpl> plugin_factory = nullptr;

   public:
    /**
     * All VST3 plugin objects we created from this plugin. We keep track of
     * these in case the plugin does a host callback, so we can associate that
     * call with the exact host context object passed to it during a call to
     * `initialize()`. The IDs here are the same IDs as generated by the Wine
     * plugin host. An instance is added here through a call by
     * `register_plugin_proxy()` in the constractor, and an instance is then
     * removed through a call to `unregister_plugin_proxy()` in the destructor.
     */
    std::map<size_t, std::reference_wrapper<Vst3PluginProxyImpl>>
        plugin_proxies;

   private:
    std::mutex plugin_proxies_mutex;

    /**
     * The IO contexts used in `send_mutually_recursive_message()` to be able to
     * execute functions from a function's calling thread while we're waiting
     * for a response. We need an entire stack of these to support mutual
     * recursion, how fun! See the docstring there for more information. When
     * this doesn't contain an IO context, this function is not being called and
     * `Vst3PlugViewProxyImpl::run_gui_task()` should post the task to
     * `Vst3PlugViewProxyImpl::run_loop_tasks`. This works exactly the same as
     * the mutual recursion handling in `Vst3Bridge`.
     */
    std::vector<std::shared_ptr<boost::asio::io_context>>
        mutual_recursion_contexts;
    std::mutex mutual_recursion_contexts_mutex;
};
