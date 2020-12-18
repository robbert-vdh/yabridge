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

#include <future>

#include "../boost-fix.h"

#include <boost/asio/dispatch.hpp>
#include <public.sdk/source/vst/hosting/module_win32.cpp>

#include "vst3-impls/host-application.h"

InstanceInterfaces::InstanceInterfaces() {}

InstanceInterfaces::InstanceInterfaces(
    Steinberg::IPtr<Steinberg::FUnknown> object)
    : object(object),
      audio_processor(object),
      component(object),
      connection_point(object),
      edit_controller(object),
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
            [&](const Vst3PluginProxy::Construct& request)
                -> Vst3PluginProxy::Construct::Response {
                Steinberg::TUID cid;
                std::copy(request.cid.begin(), request.cid.end(), cid);

                // Even though we're requesting a specific interface (to mimic
                // what the host is doing), we're immediately upcasting it to an
                // `FUnknown` so we can create a perfect proxy object.
                Steinberg::IPtr<Steinberg::FUnknown> object;
                switch (request.requested_interface) {
                    case Vst3PluginProxy::Construct::Interface::IComponent:
                        object =
                            module->getFactory()
                                .createInstance<Steinberg::Vst::IComponent>(
                                    cid);
                        break;
                    case Vst3PluginProxy::Construct::Interface::IEditController:
                        object = module->getFactory()
                                     .createInstance<
                                         Steinberg::Vst::IEditController>(cid);
                        break;
                }

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
                std::promise<void> latch;

                boost::asio::dispatch(main_context.context, [&]() {
                    // Remove the instance from within the main IO context so
                    // removing it doesn't interfere with the Win32 message loop
                    std::lock_guard lock(object_instances_mutex);
                    object_instances.erase(request.instance_id);

                    latch.set_value();
                });

                // XXX: I don't think we have to wait for the object to be
                //      deleted most of the time, but I can imagine a situation
                //      where the plugin does a host callback triggered by a
                //      Win32 timer in between where the above closure is being
                //      executed and when the actual host application context on
                //      the plugin side gets deallocated.
                latch.get_future().wait();
                return Ack{};
            },
            [&](Vst3PluginProxy::SetState& request)
                -> Vst3PluginProxy::SetState::Response {
                // This same function is defined in both `IComponent` and
                // `IEditController`, so the host is calling one or the other
                if (object_instances[request.instance_id].component) {
                    return object_instances[request.instance_id]
                        .component->setState(&request.state);
                } else {
                    return object_instances[request.instance_id]
                        .edit_controller->setState(&request.state);
                }
            },
            [&](Vst3PluginProxy::GetState& request)
                -> Vst3PluginProxy::GetState::Response {
                VectorStream stream{};
                tresult result;

                // This same function is defined in both `IComponent` and
                // `IEditController`, so the host is calling one or the other
                if (object_instances[request.instance_id].component) {
                    result = object_instances[request.instance_id]
                                 .component->getState(&stream);
                } else {
                    result = object_instances[request.instance_id]
                                 .edit_controller->getState(&stream);
                }

                return Vst3PluginProxy::GetStateResponse{
                    .result = result, .updated_state = std::move(stream)};
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
            [&](const YaConnectionPoint::Connect& request)
                -> YaConnectionPoint::Connect::Response {
                // We can directly connect the underlying objects
                // TODO: Add support for connecting objects through a proxy
                //       object provided by the host
                return object_instances[request.instance_id]
                    .connection_point->connect(
                        object_instances[request.other_instance_id]
                            .connection_point);
            },
            [&](YaEditController2::SetComponentState& request)
                -> YaEditController2::SetComponentState::Response {
                return object_instances[request.instance_id]
                    .edit_controller->setComponentState(&request.state);
            },
            [&](const YaEditController2::GetParameterCount& request)
                -> YaEditController2::GetParameterCount::Response {
                return object_instances[request.instance_id]
                    .edit_controller->getParameterCount();
            },
            [&](YaEditController2::GetParameterInfo& request)
                -> YaEditController2::GetParameterInfo::Response {
                const tresult result =
                    object_instances[request.instance_id]
                        .edit_controller->getParameterInfo(request.param_index,
                                                           request.info);

                return YaEditController2::GetParameterInfoResponse{
                    .result = result, .updated_info = request.info};
            },
            [&](YaEditController2::GetParamStringByValue& request)
                -> YaEditController2::GetParamStringByValue::Response {
                Steinberg::Vst::String128 string{0};
                const tresult result =
                    object_instances[request.instance_id]
                        .edit_controller->getParamStringByValue(
                            request.id, request.value_normalized, string);

                return YaEditController2::GetParamStringByValueResponse{
                    .result = result,
                    .string = tchar_pointer_to_u16string(string)};
            },
            [&](YaEditController2::GetParamValueByString& request)
                -> YaEditController2::GetParamValueByString::Response {
                Steinberg::Vst::ParamValue value_normalized;
                const tresult result =
                    object_instances[request.instance_id]
                        .edit_controller->getParamValueByString(
                            request.id,
                            const_cast<Steinberg::Vst::TChar*>(
                                u16string_to_tchar_pointer(
                                    request.string.c_str())),
                            value_normalized);

                return YaEditController2::GetParamValueByStringResponse{
                    .result = result, .value_normalized = value_normalized};
            },
            [&](YaPluginBase::Initialize& request)
                -> YaPluginBase::Initialize::Response {
                // If we got passed a host context, we'll create a proxy object
                // and pass that to the initialize function. This object should
                // be cleaned up again during `Vst3PluginProxy::Destruct`.
                // TODO: This needs changing if it turns out we need a
                //       `Vst3HostProxy`
                // TODO: Does this have to be run from the UI thread? Figure out
                //       if it does
                Steinberg::FUnknown* context = nullptr;
                if (request.host_application_context_args) {
                    object_instances[request.instance_id]
                        .hsot_application_context =
                        Steinberg::owned(new YaHostApplicationImpl(
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
                    Steinberg::owned(new YaHostApplicationImpl(
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
