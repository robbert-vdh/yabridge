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

PluginObject::PluginObject() {}

PluginObject::PluginObject(Steinberg::IPtr<Steinberg::FUnknown> object)
    : object(object),
      audio_processor(object),
      component(object),
      plugin_base(object) {}

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
            [&](const Vst3PluginProxy::Construct& args)
                -> Vst3PluginProxy::Construct::Response {
                Steinberg::TUID cid;
                std::copy(args.cid.begin(), args.cid.end(), cid);
                // TODO: Change this to allow creating different tyeps of
                //       objects
                Steinberg::IPtr<Steinberg::FUnknown> object =
                    module->getFactory()
                        .createInstance<Steinberg::Vst::IComponent>(cid);
                if (object) {
                    std::lock_guard lock(object_instances_mutex);

                    const size_t instance_id = generate_instance_id();
                    object_instances[instance_id] = std::move(object);

                    // This is where the magic happens. Here we deduce which
                    // interfaces are supported by this object so we can create
                    // a one-to-one proxy of it.
                    return Vst3PluginProxy::ConstructArgs(
                        object_instances[instance_id].object, instance_id);
                } else {
                    return UniversalTResult(Steinberg::kResultFalse);
                }
            },
            [&](const Vst3PluginProxy::Destruct& request)
                -> Vst3PluginProxy::Destruct::Response {
                std::lock_guard lock(object_instances_mutex);
                object_instances.erase(request.instance_id);

                return Ack{};
            },
            [&](YaAudioProcessor::SetBusArrangements& request)
                -> YaAudioProcessor::SetBusArrangements::Response {
                return object_instances[request.instance_id]
                    .audio_processor->setBusArrangements(
                        request.inputs.data(), request.num_ins,
                        request.outputs.data(), request.num_outs);
            },
            [&](YaAudioProcessor::GetBusArrangement& request)
                -> YaAudioProcessor::GetBusArrangement::Response {
                const tresult result =
                    object_instances[request.instance_id]
                        .audio_processor->getBusArrangement(
                            request.dir, request.index, request.arr);

                return YaAudioProcessor::GetBusArrangementResponse{
                    .result = result, .updated_arr = request.arr};
            },
            [&](const YaAudioProcessor::CanProcessSampleSize& request)
                -> YaAudioProcessor::CanProcessSampleSize::Response {
                return object_instances[request.instance_id]
                    .audio_processor->canProcessSampleSize(
                        request.symbolic_sample_size);
            },
            [&](const YaAudioProcessor::GetLatencySamples& request)
                -> YaAudioProcessor::GetLatencySamples::Response {
                return object_instances[request.instance_id]
                    .audio_processor->getLatencySamples();
            },
            [&](YaAudioProcessor::SetupProcessing& request)
                -> YaAudioProcessor::SetupProcessing::Response {
                return object_instances[request.instance_id]
                    .audio_processor->setupProcessing(request.setup);
            },
            [&](const YaAudioProcessor::SetProcessing& request)
                -> YaAudioProcessor::SetProcessing::Response {
                return object_instances[request.instance_id]
                    .audio_processor->setProcessing(request.state);
            },
            [&](YaAudioProcessor::Process& request)
                -> YaAudioProcessor::Process::Response {
                const tresult result =
                    object_instances[request.instance_id]
                        .audio_processor->process(request.data.get());

                return YaAudioProcessor::ProcessResponse{
                    .result = result,
                    .output_data = request.data.move_outputs_to_response()};
            },
            [&](const YaAudioProcessor::GetTailSamples& request)
                -> YaAudioProcessor::GetTailSamples::Response {
                return object_instances[request.instance_id]
                    .audio_processor->getTailSamples();
            },
            [&](const YaComponent::SetIoMode& request)
                -> YaComponent::SetIoMode::Response {
                return object_instances[request.instance_id]
                    .component->setIoMode(request.mode);
            },
            [&](const YaComponent::GetBusCount& request)
                -> YaComponent::GetBusCount::Response {
                return object_instances[request.instance_id]
                    .component->getBusCount(request.type, request.dir);
            },
            [&](YaComponent::GetBusInfo& request)
                -> YaComponent::GetBusInfo::Response {
                const tresult result =
                    object_instances[request.instance_id].component->getBusInfo(
                        request.type, request.dir, request.index, request.bus);

                return YaComponent::GetBusInfoResponse{
                    .result = result, .updated_bus = request.bus};
            },
            [&](YaComponent::GetRoutingInfo& request)
                -> YaComponent::GetRoutingInfo::Response {
                const tresult result =
                    object_instances[request.instance_id]
                        .component->getRoutingInfo(request.in_info,
                                                   request.out_info);

                return YaComponent::GetRoutingInfoResponse{
                    .result = result,
                    .updated_in_info = request.in_info,
                    .updated_out_info = request.out_info};
            },
            [&](const YaComponent::ActivateBus& request)
                -> YaComponent::ActivateBus::Response {
                return object_instances[request.instance_id]
                    .component->activateBus(request.type, request.dir,
                                            request.index, request.state);
            },
            [&](const YaComponent::SetActive& request)
                -> YaComponent::SetActive::Response {
                return object_instances[request.instance_id]
                    .component->setActive(request.state);
            },
            [&](YaComponent::SetState& request)
                -> YaComponent::SetState::Response {
                return object_instances[request.instance_id]
                    .component->setState(&request.state);
            },
            [&](YaComponent::GetState& request)
                -> YaComponent::GetState::Response {
                VectorStream stream;
                const tresult result =
                    object_instances[request.instance_id].component->getState(
                        &stream);

                return YaComponent::GetStateResponse{
                    .result = result, .updated_state = std::move(stream)};
            },
            [&](YaPluginBase::Initialize& request)
                -> YaPluginBase::Initialize::Response {
                // If we got passed a host context, we'll create a proxy object
                // and pass that to the initialize function. This object should
                // be cleaned up again during `Vst3PluginProxy::Destruct`.
                // TODO: This needs changing when we get to `Vst3HostProxy`
                Steinberg::FUnknown* context = nullptr;
                if (request.host_application_context_args) {
                    object_instances[request.instance_id]
                        .hsot_application_context =
                        Steinberg::owned(new YaHostApplicationHostImpl(
                            *this,
                            std::move(*request.host_application_context_args)));
                    context = object_instances[request.instance_id]
                                  .hsot_application_context;
                }

                return object_instances[request.instance_id]
                    .plugin_base->initialize(context);
            },
            [&](const YaPluginBase::Terminate& request)
                -> YaPluginBase::Terminate::Response {
                return object_instances[request.instance_id]
                    .plugin_base->terminate();
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
