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

#include "vst3.h"

#include "../boost-fix.h"
#include "vst3-impls.h"

#include <public.sdk/source/vst/hosting/module_win32.cpp>

Vst3Bridge::Vst3Bridge(MainContext& main_context,
                       std::string plugin_dll_path,
                       std::string endpoint_base_dir)
    : HostBridge(plugin_dll_path),
      main_context(main_context),
      sockets(main_context.context, endpoint_base_dir, false) {
    std::string error;
    module = VST3::Hosting::Win32Module::create(plugin_dll_path, error);
    if (!module) {
        throw std::runtime_error("Could not load the VST3 module for '" +
                                 plugin_dll_path + "': " + error);
    }

    sockets.connect();

    // Serialize the plugin's plugin factory. The native VST3 plugin will
    // request a copy of this during its initialization.
    plugin_factory =
        std::make_unique<YaPluginFactoryHostImpl>(module->getFactory().get());

    // Fetch this instance's configuration from the plugin to finish the setup
    // process
    config = sockets.vst_host_callback.send_message(WantsConfiguration{},
                                                    std::nullopt);
}

void Vst3Bridge::run() {
    sockets.host_vst_control.receive_messages(
        std::nullopt,
        overload{
            [&](const YaComponent::Create& args)
                -> YaComponent::Create::Response {
                Steinberg::IPtr<Steinberg::Vst::IComponent> component =
                    module->getFactory()
                        .createInstance<Steinberg::Vst::IComponent>(args.cid);

                // TODO: Next steps are:
                //       - Generate a new unique ID using an atomic size_t and
                //         fetch-and-add.
                //       - Add an `std::map<size_t,
                //         Steinberg::IPtr<Steinberg::Vst::IComponent>`
                //         to this class and add `component` with the generated
                //         ID to that.
                //       - Add that ID to `YaComponent` and set it in the object
                //         we create here.
                //       - In case `factory` is a null pointer, allow returning
                //         `nullopt`. Not sure how that is going to work with
                //         the deserialization.
                if (!component) {
                    // TODO: Handle
                }

                // TODO: Implement `YaComponentHostImpl` and create an instance
                //       based on `component`
                YaComponent* removeme = nullptr;
                return *removeme;
            },
            [&](const WantsPluginFactory&) -> WantsPluginFactory::Response {
                return *plugin_factory;
            }});
}
