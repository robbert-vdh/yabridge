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

#include <public.sdk/source/vst/hosting/module_win32.cpp>

#include "vst3-impls/host-application.h"

ComponentInstance::ComponentInstance() {}

ComponentInstance::ComponentInstance(
    Steinberg::IPtr<Steinberg::Vst::IComponent> component)
    : component(component) {}

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

    // Fetch this instance's configuration from the plugin to finish the setup
    // process
    config = sockets.vst_host_callback.send_message(WantsConfiguration{},
                                                    std::nullopt);
}

void Vst3Bridge::run() {
    // XXX: In theory all of thise should be safe assuming the host doesn't do
    //      anything weird. We're using mutexes when inserting and removing
    //      things, but for correctness we should have a multiple-readers
    //      single-writer style lock since concurrent reads and writes can also
    //      be unsafe.
    sockets.host_vst_control.receive_messages(
        std::nullopt,
        overload{
            [&](const YaComponent::Construct& args)
                -> YaComponent::Construct::Response {
                Steinberg::TUID cid;
                std::copy(args.cid.begin(), args.cid.end(), cid);
                Steinberg::IPtr<Steinberg::Vst::IComponent> component =
                    module->getFactory()
                        .createInstance<Steinberg::Vst::IComponent>(cid);
                if (component) {
                    std::lock_guard lock(component_instances_mutex);

                    const size_t instance_id = generate_instance_id();
                    component_instances[instance_id] = std::move(component);

                    return YaComponent::ConstructArgs(
                        component_instances[instance_id].component,
                        instance_id);
                } else {
                    // The actual result is lost here
                    return UniversalTResult(Steinberg::kNotImplemented);
                }
            },
            [&](const YaComponent::Destruct& request)
                -> YaComponent::Destruct::Response {
                std::lock_guard lock(component_instances_mutex);
                component_instances.erase(request.instance_id);

                return Ack{};
            },
            [&](YaComponent::Initialize& request)
                -> YaComponent::Initialize::Response {
                // If we got passed a host context, we'll create a proxy object
                // and pass that to the initialize function. This object should
                // be cleaned up again during `YaComponent::Destruct`.
                Steinberg::FUnknown* context = nullptr;
                if (request.host_application_context_args) {
                    component_instances[request.instance_id]
                        .hsot_application_context =
                        Steinberg::owned(new YaHostApplicationHostImpl(
                            *this,
                            std::move(*request.host_application_context_args)));
                    context = component_instances[request.instance_id]
                                  .hsot_application_context;
                }

                return component_instances[request.instance_id]
                    .component->initialize(context);
            },
            [&](const YaComponent::Terminate& request)
                -> YaComponent::Terminate::Response {
                return component_instances[request.instance_id]
                    .component->terminate();
            },
            [&](const YaComponent::SetIoMode& request)
                -> YaComponent::SetIoMode::Response {
                return component_instances[request.instance_id]
                    .component->setIoMode(request.mode);
            },
            [&](const YaComponent::GetBusCount& request)
                -> YaComponent::GetBusCount::Response {
                return component_instances[request.instance_id]
                    .component->getBusCount(request.type, request.dir);
            },
            [&](YaComponent::GetBusInfo& request)
                -> YaComponent::GetBusInfo::Response {
                const tresult result =
                    component_instances[request.instance_id]
                        .component->getBusInfo(request.type, request.dir,
                                               request.index, request.bus);

                return YaComponent::GetBusInfoResponse{
                    .result = result, .updated_bus = request.bus};
            },
            [&](YaComponent::GetRoutingInfo& request)
                -> YaComponent::GetRoutingInfo::Response {
                const tresult result =
                    component_instances[request.instance_id]
                        .component->getRoutingInfo(request.in_info,
                                                   request.out_info);

                return YaComponent::GetRoutingInfoResponse{
                    .result = result,
                    .updated_in_info = request.in_info,
                    .updated_out_info = request.out_info};
            },
            [&](const YaComponent::ActivateBus& request)
                -> YaComponent::ActivateBus::Response {
                return component_instances[request.instance_id]
                    .component->activateBus(request.type, request.dir,
                                            request.index, request.state);
            },
            [&](const YaComponent::SetActive& request)
                -> YaComponent::SetActive::Response {
                return component_instances[request.instance_id]
                    .component->setActive(request.state);
            },
            [&](const YaPluginFactory::Construct&)
                -> YaPluginFactory::Construct::Response {
                return YaPluginFactory::ConstructArgs(
                    module->getFactory().get());
            },
            [&](YaPluginFactory::SetHostContext& request)
                -> YaPluginFactory::SetHostContext::Response {
                plugin_factory_host_application_context =
                    Steinberg::owned(new YaHostApplicationHostImpl(
                        *this,
                        std::move(request.host_application_context_args)));

                Steinberg::FUnknownPtr<Steinberg::IPluginFactory3> factory_3(
                    module->getFactory().get());
                assert(factory_3);
                return factory_3->setHostContext(
                    plugin_factory_host_application_context);
            }});
}

size_t Vst3Bridge::generate_instance_id() {
    return current_instance_id.fetch_add(1);
}
