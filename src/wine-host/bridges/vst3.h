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

#include <iostream>
#include <string>

#include <public.sdk/source/vst/hosting/module.h>

#include "../../common/communication/vst3.h"
#include "../../common/configuration.h"
#include "../editor.h"
#include "common.h"

// Forward declarations
class Vst3ContextMenuProxyImpl;

/**
 * A holder for an object instance's `IPlugView` object and all smart pointers
 * casted from it.
 *
 * @relates InstanceInterfaces
 */
struct InstancePlugView {
    InstancePlugView();

    InstancePlugView(Steinberg::IPtr<Steinberg::IPlugView> plug_View);

    Steinberg::IPtr<Steinberg::IPlugView> plug_view;

    // All smart pointers below are created from `plug_view`. They will be null
    // pointers if `plug_view` did not implement the interface.

    Steinberg::FUnknownPtr<Steinberg::Vst::IParameterFinder> parameter_finder;
};

/**
 * A holder for plugin object instance created from the factory. This stores all
 * relevant interface smart pointers to that object so we can handle control
 * messages sent by the plugin without having to do these expensive casts all
 * the time. This also stores any additional context data, such as the
 * `IHostApplication` instance passed to the plugin during
 * `IPluginBase::initialize()`.
 */
struct InstanceInterfaces {
    InstanceInterfaces();

    InstanceInterfaces(Steinberg::IPtr<Steinberg::FUnknown> object);

    /**
     * A dedicated thread for handling incoming `IAudioProcessor` and
     * `IComponent` calls. Will be instantiated if `object` supports either of
     * those interfaces.
     */
    Win32Thread audio_processor_handler;

    /**
     * If the host passes a host context object during
     * `IPluginBase::initialize()`, we'll store a proxy object here and then
     * pass it to `plugin_base->initialize()`. Will be initialized with a null
     * pointer until used.
     */
    Steinberg::IPtr<Vst3HostContextProxy> host_context_proxy;

    /**
     * If the host connects two objects indirectly using a connection proxy (as
     * allowed by the VST3 specification), then we also can't connect the
     * objects directly on the Wine side. In that case we'll have to create this
     * proxy object, pass it to the plugin, and if the plugin then calls
     * `IConnectionPoint::notify()` on it we'll pass that call through to the
     * `IConnectionPoint` instance passed to us by the host (which will then in
     * turn call `IConnectionPoint::notify()` on our plugin proxy object).
     * Proxies for days.
     */
    Steinberg::IPtr<Vst3ConnectionPointProxy> connection_point_proxy;

    /**
     * After a call to `IEditController::setComponentHandler()`, we'll create a
     * proxy of that component handler just like we did for the plugin object.
     * When the plugin calls a function on this object, we make a callback to
     * the original object provided by the host. Will be initialized with a null
     * pointer until used.
     */
    Steinberg::IPtr<Vst3ComponentHandlerProxy> component_handler_proxy;

    /**
     * If the host passes an `IPlugFrame` object during `IPlugView::setFrame()`,
     * then we'll store a proxy object here and then pass it to
     * `plug_view->setFrame()`. Will be initialized with a null pointer until
     * used. When we destroy `plug_view` while handling
     * `Vst3PlugViewProxy::Destruct`, we'll also destroy (our pointer of) this
     * proxy object.
     */
    Steinberg::IPtr<Vst3PlugFrameProxy> plug_frame_proxy;

    /**
     * Currently active context menu proxy instances. A call to
     * `IComponentHandler3::createContextMenu` by the plugin will create a proxy
     * object for the actual context menu returned by the host. We'll use this
     * map to refer to a specific context menu later when the host wants to
     * execute a specific menu item.
     *
     * @relates Vst3Bridge::register_context_menu
     * @relates Vst3Bridge::unregister_context_menu
     */
    std::map<size_t, std::reference_wrapper<Vst3ContextMenuProxyImpl>>
        registered_context_menus;
    std::mutex registered_context_menus_mutex;

    /**
     * The base object we cast from.
     */
    Steinberg::IPtr<Steinberg::FUnknown> object;

    /**
     * The `IPlugView` object the plugin returned from a call to
     * `IEditController::createView()`.
     *
     * XXX: Technically VST3 supports multiple named views, so we could have
     *      multiple different view for a single plugin. This is not used within
     *      the SDK, so a single pointer should be fine for now.
     */
    std::optional<InstancePlugView> plug_view_instance;

    /**
     * This instance's editor, if it has an open editor. Embedding here works
     * exactly the same as how it works for VST2 plugins.
     */
    std::optional<Editor> editor;

    // All smart pointers below are created from `component`. They will be null
    // pointers if `component` did not implement the interface.

    Steinberg::FUnknownPtr<Steinberg::Vst::IAudioPresentationLatency>
        audio_presentation_latency;
    Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor> audio_processor;
    Steinberg::FUnknownPtr<Steinberg::Vst::IComponent> component;
    Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint> connection_point;
    Steinberg::FUnknownPtr<Steinberg::Vst::IEditController> edit_controller;
    Steinberg::FUnknownPtr<Steinberg::Vst::IEditController2> edit_controller_2;
    Steinberg::FUnknownPtr<Steinberg::Vst::IEditControllerHostEditing>
        edit_controller_host_editing;
    Steinberg::FUnknownPtr<Steinberg::Vst::IKeyswitchController>
        keyswitch_controller;
    Steinberg::FUnknownPtr<Steinberg::Vst::IMidiMapping> midi_mapping;
    Steinberg::FUnknownPtr<Steinberg::Vst::INoteExpressionController>
        note_expression_controller;
    Steinberg::FUnknownPtr<Steinberg::IPluginBase> plugin_base;
    Steinberg::FUnknownPtr<Steinberg::Vst::IUnitData> unit_data;
    Steinberg::FUnknownPtr<Steinberg::Vst::IProgramListData> program_list_data;
    Steinberg::FUnknownPtr<Steinberg::Vst::IUnitInfo> unit_info;
    Steinberg::FUnknownPtr<Steinberg::Vst::IXmlRepresentationController>
        xml_representation_controller;
};

/**
 * This hosts a Windows VST3 plugin, forwards messages sent by the Linux VST
 * plugin and provides host callback function for the plugin to talk back.
 */
class Vst3Bridge : public HostBridge {
   public:
    /**
     * Initializes the Windows VST3 plugin and set up communication with the
     * native Linux VST plugin.
     *
     * @param main_context The main IO context for this application. Most events
     *   will be dispatched to this context, and the event handling loop should
     *   also be run from this context.
     * @param plugin_dll_path A (Unix style) path to the VST plugin .dll file to
     *   load.
     * @param endpoint_base_dir The base directory used for the socket
     *   endpoints. See `Sockets` for more information.
     *
     * @note The object has to be constructed from the same thread that calls
     *   `main_context.run()`.
     *
     * @throw std::runtime_error Thrown when the VST plugin could not be loaded,
     *   or if communication could not be set up.
     */
    Vst3Bridge(MainContext& main_context,
               std::string plugin_dll_path,
               std::string endpoint_base_dir);

    /**
     * Here we'll listen for and handle incoming control messages until the
     * sockets get closed.
     */
    void run() override;

    void handle_x11_events() override;
    void handle_win32_events() override;

    /**
     * Send a callback message to the host return the response. This is a
     * shorthand for `sockets.vst_host_callback.send_message` for use in VST3
     * interface implementations.
     */
    template <typename T>
    typename T::Response send_message(const T& object) {
        return sockets.vst_host_callback.send_message(object, std::nullopt);
    }

    /**
     * Spawn a new thread and call `send_message()` from there, and then handle
     * functions passed by calls to
     * `do_mutual_recursion_or_handle_in_main_context()` on this thread until
     * the original message we're trying to send has succeeded. This is a very
     * specific solution to a very specific problem. When a plugin wants to
     * resize itself, it will call `IPlugFrame::resizeView()` from within the
     * WIn32 message loop. The host will then call `IPlugView::onSize()` on the
     * plugin's `IPlugView` to actually resize the plugin. The issue is that
     * that call to `IPlugView::onSize()` has to be handled from the UI thread,
     * but in this sequence that thread is being blocked by a call to
     * `IPlugFrame::resizeView()`.
     *
     * The hacky solution here is to send the message from another thread, and
     * to then allow this thread to execute other functions submitted to an IO
     * context.
     */
    template <typename T>
    typename T::Response send_mutually_recursive_message(const T& object) {
        using TResponse = typename T::Response;

        // This IO context will accept incoming calls from
        // `do_mutual_recursion_or_handle_in_main_context()` until we receive a
        // response
        {
            std::unique_lock lock(mutual_recursion_context_mutex);

            // In case some other thread is already calling
            // `send_mutually_recursive_message()` (which should never be the
            // case since this should only be called from the UI thread), we'll
            // wait for that function to finish
            if (mutual_recursion_context) {
                mutual_recursion_context_cv.wait(lock, [&]() {
                    return !mutual_recursion_context.has_value();
                });
            }

            mutual_recursion_context.emplace();
        }

        // We will call the function from another thread so we can handle calls
        // to  from this thread
        std::promise<TResponse> response_promise{};
        Win32Thread sending_thread([&]() {
            const TResponse response = send_message(object);

            // Stop accepting additional work to be run from the calling thread
            // once we receive a response
            {
                std::lock_guard lock(mutual_recursion_context_mutex);
                mutual_recursion_context->stop();
                mutual_recursion_context.reset();
            }
            mutual_recursion_context_cv.notify_one();

            response_promise.set_value(response);
        });

        // Accept work from the other thread until we receive a response, at
        // which point the context will be stopped
        auto work_guard =
            boost::asio::make_work_guard(*mutual_recursion_context);
        mutual_recursion_context->run();

        return response_promise.get_future().get();
    }

    /**
     * Crazy functions ask for crazy naming. This is the other part of
     * `send_mutually_recursive_message()`. If another thread is currently
     * calling that function (from the UI thread), then we'll execute `f` from
     * the UI thread using the IO context started in the above function.
     * Otherwise `f` will be run on the UI thread through `main_context` as
     * usual.
     *
     * @see Vst3Bridge::send_mutually_recursive_message
     */
    template <typename T, typename F>
    T do_mutual_recursion_or_handle_in_main_context(F f) {
        std::packaged_task<T()> do_call(f);
        std::future<T> do_call_response = do_call.get_future();

        // If the above function is currently being called from some thread,
        // then we'll submit the task to the IO context created there so it can
        // be handled on that same thread. Otherwise we'll just submit it to the
        // main IO context. Neither of these two functions block until `do_call`
        // finish executing.
        {
            std::lock_guard lock(mutual_recursion_context_mutex);
            if (mutual_recursion_context) {
                boost::asio::dispatch(*mutual_recursion_context,
                                      std::move(do_call));
            } else {
                main_context.schedule_task(std::move(do_call));
            }
        }

        return do_call_response.get();
    }

    /**
     * Register a context with with `context_menu`'s ID and owner in
     * `object_instances`. This will be called during the constructor of
     * `Vst3ContextMenuProxyImpl` so we can refer to the exact instance later.
     */
    void register_context_menu(Vst3ContextMenuProxyImpl& context_menu);

    /**
     * Remove a previously registered context menu from `object_instances`. This
     * is called from the destructor of `Vst3ContextMenuProxyImpl` just before
     * the object gets freed.
     */
    void unregister_context_menu(size_t object_instance_id,
                                 size_t context_menu_id);

   private:
    Logger generic_logger;

   public:
    /**
     * A logger instance we'll use to log about failed
     * `FUnknown::queryInterface` calls, so they can be hidden on verbosity
     * level 0.
     *
     * This only has to be used instead of directly writing to `std::cerr` when
     * the message should be hidden on lower verbosity levels.
     */
    Vst3Logger logger;

   private:
    /**
     * Generate a nique instance identifier using an atomic fetch-and-add. This
     * is used to be able to refer to specific instances created for
     * `IPluginFactory::createInstance()`.
     */
    size_t generate_instance_id();

    /**
     * Assign a unique identifier to an object and add it to `object_instances`.
     * This will also set up listeners for `IAudioProcessor` and `IComponent`
     * function calls.
     */
    size_t register_object_instance(
        Steinberg::IPtr<Steinberg::FUnknown> object);

    /**
     * Remove an object from `object_instances`. Will also tear down the
     * `IAudioProcessor`/`IComponent` socket if it had one.
     */
    void unregister_object_instance(size_t instance_id);

    /**
     * The IO context used for event handling so that all events and window
     * message handling can be performed from a single thread, even when hosting
     * multiple plugins.
     */
    MainContext& main_context;

    /**
     * The configuration for this instance of yabridge based on the `.so` file
     * that got loaded by the host. This configuration gets loaded on the plugin
     * side, and then sent over to the Wine host as part of the startup process.
     */
    Configuration config;

    std::shared_ptr<VST3::Hosting::Module> module;

    /**
     * All sockets used for communicating with this specific plugin.
     *
     * NOTE: This is defined **after** the threads on purpose. This way the
     *       sockets will be closed first, and we can then safely wait for the
     *       threads to exit.
     */
    Vst3Sockets<Win32Thread> sockets;

    /**
     * Used to assign unique identifiers to instances created for
     * `IPluginFactory::createInstance()`.
     *
     * @related enerate_instance_id
     */
    std::atomic_size_t current_instance_id;

    /**
     * The host context proxy object if we got passed a host context during a
     * call to `IPluginFactory3::setHostContext()` by the host.
     */
    Steinberg::IPtr<Vst3HostContextProxy> plugin_factory_host_context;

    /**
     * These are all the objects we have created through the Windows VST3
     * plugins' plugin factory. The keys in all of these maps are the unique
     * identifiers we generated for them so we can identify specific instances.
     * During the proxy object's destructor (on the plugin side), we'll get a
     * request to remove the corresponding plugin object from this map. This
     * will cause all pointers to it to get dropped and the object to be cleaned
     * up.
     */
    std::map<size_t, InstanceInterfaces> object_instances;
    std::mutex object_instances_mutex;

    /**
     * The IO context used in `send_mutually_recursive_message()` to be able to
     * execute functions from that same calling thread while we're waiting for a
     * response. See the docstring there for more information. When this doesn't
     * contain an IO context, this function is not being called and
     * `do_mutual_recursion_or_handle_in_main_context()` should post the task
     * directly to the main IO context.
     */
    std::optional<boost::asio::io_context> mutual_recursion_context;
    std::mutex mutual_recursion_context_mutex;
    /**
     * Used to make sure only a single call to
     * `send_mutually_recursive_message()` at a time can be processed (this
     * should never happen, but better safe tha nsorry).
     */
    std::condition_variable mutual_recursion_context_cv;
};
