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

#include "src/common/serialization/vst3.h"
#include "vst3-impls/plugin-factory.h"
#include "vst3-impls/plugin-proxy.h"

// There are still some design decisions that need some more thought
// TODO: Check whether `IPlugView::isPlatformTypeSupported` needs special
//       handling.
// TODO: The documentation mentions that private communication through VST3's
//       message system should be handled on a separate timer thread.  Do we
//       need special handling for this on the Wine side (e.g. during the event
//       handling loop)? Probably not, since the actual host should manage all
//       messaging.
// TODO: The docs very explicitly mention that
//       the`IComponentHandler::{begin,perform,end}Edit()` functions have to be
//       called from the UI thread. Should we have special handling for this or
//       does everything just magically work out?
// TODO: Something that's not relevant here but that will require some thinking
//       is that VST3 requires all plugins to be installed in ~/.vst3. I can
//       think of two options and I"m not sure what's the best one:
//
//       1. We can add the required files for the Linux VST3 plugin to the
//          location of the Windows VST3 plugin (by adding some files to the
//          bundle or creating a bundle next to it) and then symlink that bundle
//          to ~/.vst3.
//       2. We can create the bundle in ~/.vst3 and symlink the Windows plugin
//          and all of its resources into bundle as if they were also installed
//          there.
//
//       The second one sounds much better, but it will still need some more
//       consideration. Aside from that VST3 plugins also have a centralized
//       preset location, even though barely anyone uses it, yabridgectl will
//       also have to make a symlink of that. Also, yabridgectl will need to do
//       some extra work there to detect removed plugins.
// TODO: Also symlink presets, and allow pruning broken symlinks there as well
// TODO: And how do we choose between 32-bit and 64-bit versions of a VST3
//       plugin if they exist? Config files?

Vst3PluginBridge::Vst3PluginBridge()
    : PluginBridge(
          PluginType::vst3,
          [](boost::asio::io_context& io_context, const PluginInfo& info) {
              return Vst3Sockets<std::jthread>(
                  io_context,
                  generate_endpoint_base(info.native_library_path.filename()
                                             .replace_extension("")
                                             .string()),
                  true);
          }),
      logger(generic_logger) {
    log_init_message();

    // This will block until all sockets have been connected to by the Wine VST
    // host
    connect_sockets_guarded();

    // Now that communication is set up the Wine host can send callbacks to this
    // bridge class, and we can send control messages to the Wine host. This
    // messaging mechanism is how we relay the VST3 communication protocol. As a
    // first thing, the Wine VST host will ask us for a copy of the
    // configuration.
    host_callback_handler = std::jthread([&]() {
        sockets.vst_host_callback.receive_messages(
            std::pair<Vst3Logger&, bool>(logger, false),
            overload{
                [&](const WantsConfiguration&) -> WantsConfiguration::Response {
                    return config;
                },
                [&](const YaComponentHandler::BeginEdit& request)
                    -> YaComponentHandler::BeginEdit::Response {
                    return plugin_proxies.at(request.owner_instance_id)
                        .get()
                        .component_handler->beginEdit(request.id);
                },
                [&](const YaComponentHandler::PerformEdit& request)
                    -> YaComponentHandler::PerformEdit::Response {
                    return plugin_proxies.at(request.owner_instance_id)
                        .get()
                        .component_handler->performEdit(
                            request.id, request.value_normalized);
                },
                [&](const YaComponentHandler::EndEdit& request)
                    -> YaComponentHandler::EndEdit::Response {
                    return plugin_proxies.at(request.owner_instance_id)
                        .get()
                        .component_handler->endEdit(request.id);
                },
                [&](const YaComponentHandler::RestartComponent& request)
                    -> YaComponentHandler::EndEdit::Response {
                    return plugin_proxies.at(request.owner_instance_id)
                        .get()
                        .component_handler->restartComponent(request.flags);
                },
            });
    });
}

Vst3PluginBridge::~Vst3PluginBridge() {
    // Drop all work make sure all sockets are closed
    plugin_host->terminate();
    io_context.stop();
}

Steinberg::IPluginFactory* Vst3PluginBridge::get_plugin_factory() {
    // Even though we're working with raw pointers here, we should pretend that
    // we're `IPtr<Steinberg::IPluginFactory>` and do the reference counting
    // ourselves. This should work the same was as the standard implementation
    // in `public.sdk/source/main/pluginfactory.h`. If we were to use an IPtr or
    // an STL smart pointer we would get a double free (or rather, a use after
    // free).
    if (plugin_factory) {
        plugin_factory->addRef();
    } else {
        // Set up the plugin factory, since this is the first thing the host
        // will request after loading the module. Host callback handlers should
        // have started before this since the Wine plugin host will request a
        // copy of the configuration during its initialization.
        YaPluginFactory::ConstructArgs factory_args =
            sockets.host_vst_control.send_message(
                YaPluginFactory::Construct{},
                std::pair<Vst3Logger&, bool>(logger, true));
        plugin_factory =
            new YaPluginFactoryImpl(*this, std::move(factory_args));
    }

    return plugin_factory;
}

void Vst3PluginBridge::register_plugin_proxy(Vst3PluginProxyImpl& component) {
    std::lock_guard lock(plugin_proxies_mutex);
    plugin_proxies.emplace(component.instance_id(),
                           std::ref<Vst3PluginProxyImpl>(component));
}

void Vst3PluginBridge::unregister_plugin_proxy(size_t instance_id) {
    std::lock_guard lock(plugin_proxies_mutex);
    plugin_proxies.erase(instance_id);
}
