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
#include "../../common/mutual-recursion.h"
#include "../editor.h"
#include "common.h"

// Forward declarations
class Vst3ContextMenuProxyImpl;

/**
 * A holder for an object instance's `IPlugView` object and all smart pointers
 * casted from it.
 *
 * @relates Vst3PluginInstance
 */
struct Vst3PlugViewInterfaces {
    Vst3PlugViewInterfaces() noexcept;

    Vst3PlugViewInterfaces(
        Steinberg::IPtr<Steinberg::IPlugView> plug_View) noexcept;

    Steinberg::IPtr<Steinberg::IPlugView> plug_view;

    // All smart pointers below are created from `plug_view`. They will be null
    // pointers if `plug_view` did not implement the interface.

    Steinberg::FUnknownPtr<Steinberg::Vst::IParameterFinder> parameter_finder;
    Steinberg::FUnknownPtr<Steinberg::IPlugViewContentScaleSupport>
        plug_view_content_scale_support;
};

/**
 * Smart pointers for all interfaces a VST3 plugin object might support.
 *
 * @relates Vst3PluginInstance
 */
struct Vst3PluginInterfaces {
    Vst3PluginInterfaces(Steinberg::IPtr<Steinberg::FUnknown> object) noexcept;

    Steinberg::FUnknownPtr<Steinberg::Vst::IAudioPresentationLatency>
        audio_presentation_latency;
    Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor> audio_processor;
    Steinberg::FUnknownPtr<Steinberg::Vst::IAutomationState> automation_state;
    Steinberg::FUnknownPtr<Steinberg::Vst::IComponent> component;
    Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint> connection_point;
    Steinberg::FUnknownPtr<Steinberg::Vst::IEditController> edit_controller;
    Steinberg::FUnknownPtr<Steinberg::Vst::IEditController2> edit_controller_2;
    Steinberg::FUnknownPtr<Steinberg::Vst::IEditControllerHostEditing>
        edit_controller_host_editing;
    Steinberg::FUnknownPtr<Steinberg::Vst::ChannelContext::IInfoListener>
        info_listener;
    Steinberg::FUnknownPtr<Steinberg::Vst::IKeyswitchController>
        keyswitch_controller;
    Steinberg::FUnknownPtr<Steinberg::Vst::IMidiLearn> midi_learn;
    Steinberg::FUnknownPtr<Steinberg::Vst::IMidiMapping> midi_mapping;
    Steinberg::FUnknownPtr<Steinberg::Vst::INoteExpressionController>
        note_expression_controller;
    Steinberg::FUnknownPtr<Steinberg::Vst::INoteExpressionPhysicalUIMapping>
        note_expression_physical_ui_mapping;
    Steinberg::FUnknownPtr<Steinberg::IPluginBase> plugin_base;
    Steinberg::FUnknownPtr<Steinberg::Vst::IUnitData> unit_data;
    Steinberg::FUnknownPtr<Steinberg::Vst::IParameterFunctionName>
        parameter_function_name;
    Steinberg::FUnknownPtr<Steinberg::Vst::IPrefetchableSupport>
        prefetchable_support;
    Steinberg::FUnknownPtr<Steinberg::Vst::IProcessContextRequirements>
        process_context_requirements;
    Steinberg::FUnknownPtr<Steinberg::Vst::IProgramListData> program_list_data;
    Steinberg::FUnknownPtr<Steinberg::Vst::IUnitInfo> unit_info;
    Steinberg::FUnknownPtr<Steinberg::Vst::IXmlRepresentationController>
        xml_representation_controller;
};

/**
 * A holder for plugin object instance created from the factory. This contains a
 * smart pointer to the object's `FUnknown` interface and everything else we
 * need to proxy for this object, like audio threads and proxy objects for
 * callbacks. We also store an `interfaces` object that contains smart pointers
 * to all relevant VST3 interface so we can handle control messages sent by the
 * plugin without having to do these expensive casts all the time.
 */
struct Vst3PluginInstance {
    Vst3PluginInstance(Steinberg::IPtr<Steinberg::FUnknown> object) noexcept;

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
     * A shared memory object we'll write the input audio buffers to on the
     * native plugin side. We'll then let the plugin write its outputs here on
     * the Wine side. The buffer will be configured during
     * `IAudioProcessor::setupProcessing()`. At that point we'll build the
     * configuration for the object here, on the Wine side, and then we'll
     * initialize the buffers using that configuration. This same configuration
     * is then used on the native plugin side to connect to this same shared
     * memory object for the matching plugin instance.
     */
    std::optional<AudioShmBuffer> process_buffers;

    /**
     * Pointers to the per-bus input channels in process_buffers so we can pass
     * them to the plugin after a call to `YaProcessData::reconstruct()`. These
     * can be either `float*` or `double*`, so we sadly have to use void
     * pointers here.
     */
    std::vector<std::vector<void*>> process_buffers_input_pointers;

    /**
     * Pointers to the per-bus output channels in process_buffers so we can pass
     * them to the plugin after a call to `YaProcessData::reconstruct()`. These
     * can be either `float*` or `double*`, so we sadly have to use void
     * pointers here.
     */
    std::vector<std::vector<void*>> process_buffers_output_pointers;

    /**
     * This instance's editor, if it has an open editor. Embedding here works
     * exactly the same as how it works for VST2 plugins.
     */
    std::optional<Editor> editor;

    /**
     * The base object we cast from. This is upcasted form the object created by
     * the factory.
     */
    Steinberg::IPtr<Steinberg::FUnknown> object;

    /**
     * The `IPlugView` object the plugin returned from a call to
     * `IEditController::createView()`. This object can also implement multiple
     * interfaces.
     *
     * XXX: Technically VST3 supports multiple named views, so we could have
     *      multiple different view for a single plugin. This is not used within
     *      the SDK, so a single pointer should be fine for now.
     */
    std::optional<Vst3PlugViewInterfaces> plug_view_instance;

    /**
     * Used to make sure that `IPlugView::getSize()` can never be called at the
     * same time as `IAudioProcessor::setProcessing()`.
     *
     * HACK: This really shouldn't be needed, but MeldaProduction plugins seem
     *       to deadlock when this happens. It's pretty tricky to reproduce the
     *       timing for making this happen (since opening the GUI also needs to
     *       be delayed slightly, like when opening a plugin from Bitwig's popup
     *       browser), but it seems like a good idea to make sure that this
     *       doesn't cause any freezes.
     */
    std::recursive_mutex get_size_mutex;

    /**
     * This contains smart pointers to all VST3 plugin interfaces that can be
     * casted from `object`.
     */
    Vst3PluginInterfaces interfaces;

    /**
     * Whether `IPluginBase:initialize()` has already been called for this
     * object instance. If the object doesn't implement `IPluginBase` then this
     * will always be true. I haven't run into any VST3 plugins that have issues
     * with partially initialized states like the VST2 versions of T-RackS 5
     * have, but we'll just do this out of precaution.
     */
    bool is_initialized = false;
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
     * @param parent_pid The process ID of the native plugin host this bridge is
     *   supposed to communicate with. Used as part of our watchdog to prevent
     *   dangling Wine processes.
     *
     * @note The object has to be constructed from the same thread that calls
     *   `main_context.run()`.
     *
     * @throw std::runtime_error Thrown when the VST plugin could not be loaded,
     *   or if communication could not be set up.
     */
    Vst3Bridge(MainContext& main_context,
               std::string plugin_dll_path,
               std::string endpoint_base_dir,
               pid_t parent_pid);

    /**
     * For VST3 plugins we'll have to check for every object in
     * `object_instances` that supports `IPluginBase` whether
     * `IPluginBase::iniitalize()` has been called.
     */
    bool inhibits_event_loop() noexcept override;

    /**
     * Here we'll listen for and handle incoming control messages until the
     * sockets get closed.
     */
    void run() override;

    void handle_x11_events() noexcept override;

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

   protected:
    void close_sockets() override;

   public:
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
     * When called form the GUI thread, spawn a new thread and call
     * `send_message()` from there, and then handle functions passed by calls to
     * `do_mutual_recursion_on_gui_thread()` and
     * `do_mutual_recursion_on_off_thread()` on this thread until we get a
     * response back. This is a very specific solution to a very specific
     * problem. When a plugin wants to resize itself, it will call
     * `IPlugFrame::resizeView()` from within the WIn32 message loop. The host
     * will then call `IPlugView::onSize()` on the plugin's `IPlugView` to
     * actually resize the plugin. The issue is that that call to
     * `IPlugView::onSize()` has to be handled from the UI thread, but in this
     * sequence that thread is being blocked by a call to
     * `IPlugFrame::resizeView()`.
     *
     * We also need to use this for when a plugin calls
     * `IComponentHandler::restartComponent()` to change the latency, and when
     * the host then calls `IAudioProcessor::setActive()` in response to that to
     * restart the plugin. Otherwise this will lead to an infinite loop.
     *
     * The hacky solution here is to send the message from another thread, and
     * to then allow this thread to execute other functions submitted to an IO
     * context.
     *
     * We apply the same trick in `Vst3HostBridge`.
     *
     * NOTE: This is meant to allow mutually recursive call chains where every
     *       function is called from and handled on the GUI thread. JUCE calls
     *       `IComponentHandler::performEdit` from the audio thread because they
     *       didn't implement the VST3 output parameters, and if at the same
     *       time a resize request comes in from the host that would mean that
     *       the resize request is also called from the audio thread. To prevent
     *       this we need to have two separate mutual recursion stacks for the
     *       GUI thread and for other threads. See the docstring on
     *       `audio_thread_mutual_recursion` for why _that_ is necessary.
     */
    template <typename T>
    typename T::Response send_mutually_recursive_message(const T& object) {
        if (main_context.is_gui_thread()) {
            return mutual_recursion.fork(
                [&]() { return send_message(object); });
        } else {
            return audio_thread_mutual_recursion.fork(
                [&]() { return send_message(object); });
        }
    }

    /**
     * Crazy functions ask for crazy naming. This is the other part of
     * `send_mutually_recursive_message()`, for executing mutually recursive
     * functions on the GUI thread. If another thread is currently calling that
     * function (from the UI thread), then we'll execute `fn` from the UI thread
     * using the IO context started in the above function. Otherwise `f` will be
     * run on the UI thread through `main_context` as usual.
     *
     * @see Vst3Bridge::send_mutually_recursive_message
     */
    template <std::invocable F>
    std::invoke_result_t<F> do_mutual_recursion_on_gui_thread(F&& fn) {
        // If the above function is currently being called from some thread,
        // then we'll call `fn` from that same thread. Otherwise we'll just
        // submit it to the main IO context.
        if (const auto result =
                mutual_recursion.maybe_handle(std::forward<F>(fn))) {
            return *result;
        } else {
            return main_context.run_in_context(std::forward<F>(fn)).get();
        }
    }

    /**
     * The same as the above function, but we'll just execute the function on
     * this thread when the mutual recursion context is not active.
     *
     * @see Vst3Bridge::do_mutual_recursion_on_gui_thread
     */
    template <std::invocable F>
    std::invoke_result_t<F> do_mutual_recursion_on_off_thread(F&& fn) {
        if (const auto result = audio_thread_mutual_recursion.maybe_handle(
                std::forward<F>(fn))) {
            return *result;
        } else {
            return mutual_recursion.handle(std::forward<F>(fn));
        }
    }

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
    size_t generate_instance_id() noexcept;

    /**
     * Sets up the shared memory audio buffers for a plugin instance plugin
     * instance and return the configuration so the native plugin can connect to
     * it as well.
     */
    AudioShmBuffer::Config setup_shared_audio_buffers(
        size_t instance_id,
        const Steinberg::Vst::ProcessSetup& setup);

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
    std::unordered_map<size_t, Vst3PluginInstance> object_instances;
    std::mutex object_instances_mutex;

    /**
     * Used in `send_mutually_recursive_message()` to be able to execute
     * functions from that same calling thread (through
     * `do_mutual_recursion_on_gui_thread()` and
     * `do_mutual_recursion_on_off_thread()`) while we're waiting for a
     * response.
     */
    MutualRecursionHelper<Win32Thread> mutual_recursion;

    /**
     * The same thing as above, but just for the pair of
     * `IEditController::setParamNormalized()` and
     * `IComponentHandler::performEdit()`, when
     * `IComponentHandler::performEdit()` is called from an audio thread.
     *
     * HACK: This is sadly needed to work around an interaction between a bug in
     *       JUCE with a bug in Ardour/Mixbus. JUCE calls
     *       `IComponentHandler::performEdit()` from the audio thread instead of
     *       using the output parameters, and Ardour/Mixbus immediately call
     *       `IEditController::setParamNormalized()` with the same value after
     *       the plugin calls `IComponentHandler::performEdit()`. Both of these
     *       functions need to be run on the same thread (because of recursive
     *       mutexes), but they may not interfere with the GUI thread if
     *       `IComponentHandler::performEdit()` wasn't called from there.
     */
    MutualRecursionHelper<Win32Thread> audio_thread_mutual_recursion;
};
