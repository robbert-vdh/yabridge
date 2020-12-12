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

#include <string>

#include <public.sdk/source/vst/hosting/module.h>

#include "../../common/communication/vst3.h"
#include "../../common/configuration.h"
#include "common.h"

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

   private:
    /**
     * Generate a nique instance identifier using an atomic fetch-and-add. This
     * is used to be able to refer to specific instances created for
     * `IPluginFactory::createInstance()`.
     */
    size_t generate_instance_id();

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
     * Used to assign unique identifier to instances created for
     * `IPluginFactory::createInstance()`.
     *
     * @related enerate_instance_id
     */
    std::atomic_size_t current_instance_id;

    // Below are managed instances we created for
    // `IPluginFactory::createInstance()`. The keys in all of these maps are the
    // unique identifiers we generated for them so we can identify specific
    // instances. The mutexes are used for operations that insert or remove
    // items, and not for regular access.

    std::map<size_t, Steinberg::IPtr<Steinberg::Vst::IComponent>>
        component_instances;
    std::mutex component_instances_mutex;
};
