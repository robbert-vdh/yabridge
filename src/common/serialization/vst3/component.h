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

#include <optional>
#include <set>
#include <variant>

#include <bitsery/ext/pointer.h>
#include <bitsery/ext/std_optional.h>
#include <bitsery/ext/std_set.h>
#include <bitsery/ext/std_variant.h>
#include <bitsery/traits/array.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstcomponent.h>

#include "../../bitsery/ext/vst3.h"
#include "../common.h"
#include "base.h"
#include "host-application.h"
#include "process-data.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IComponent` for serialization purposes. See `README.md` for
 * more information on how this works. On the Wine plugin host side this is only
 * used for serialization, and on the plugin side have an implementation that
 * can send control messages.
 *
 * We might be able to do some caching here with the buss infos, but since that
 * sounds like a huge potential source of errors we'll just do pure callbacks
 * for everything other than the edit controller's class ID.
 *
 * TODO: Amplement IConnectionPoint
 * TODO: How should we support IComponents without a seperate edit controller?
 *       Can we just use a separate `YaEditController` that just points to the
 *       same implementation (with the same CID)? Check the reference
 *       implementation in the framework to see how this is initialized, make
 *       sure we support the reference w workflow.
 */
class YaComponent : public Steinberg::Vst::IComponent,
                    public Steinberg::Vst::IAudioProcessor {
   public:
    /**
     * These are the arguments for creating a `YaComponentPluginImpl`.
     */
    struct ConstructArgs {
        ConstructArgs();

        /**
         * Read arguments from an existing implementation. Depending on the
         * supported interface function more or less of this struct will be left
         * empty, and `known_iids` will be set accordingly.
         */
        ConstructArgs(Steinberg::IPtr<Steinberg::Vst::IComponent> component,
                      size_t instance_id);

        /**
         * The unique identifier for this specific instance.
         */
        native_size_t instance_id;

        /**
         * The IIDs that the interface we serialized supports.
         */
        std::set<Steinberg::FUID> known_iids;

        /**
         * The class ID of this component's corresponding editor controller. You
         * can't use C-style array in `std::optional`s.
         */
        std::optional<ArrayUID> edit_controller_cid;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.ext(known_iids, bitsery::ext::StdSet{32},
                  [](S& s, Steinberg::FUID& iid) {
                      s.ext(iid, bitsery::ext::FUID{});
                  });
            s.ext(edit_controller_cid, bitsery::ext::StdOptional{},
                  [](S& s, auto& cid) { s.container1b(cid); });
        }
    };

    /**
     * Message to request the Wine plugin host to instantiate a new IComponent
     * to pass through a call to `IComponent::createInstance(cid,
     * IComponent::iid,
     * ...)`.
     */
    struct Construct {
        using Response = std::variant<ConstructArgs, UniversalTResult>;

        ArrayUID cid;

        template <typename S>
        void serialize(S& s) {
            s.container1b(cid);
        }
    };

    /**
     * Instantiate this instance with arguments read from another interface
     * implementation.
     */
    YaComponent(const ConstructArgs&& args);

    /**
     * Message to request the Wine plugin host to destroy the IComponent
     * instance with the given instance ID. Sent from the destructor of
     * `YaComponentPluginImpl`.
     */
    struct Destruct {
        using Response = Ack;

        native_size_t instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
        }
    };

    /**
     * @remark The plugin side implementation should send a control message to
     *   clean up the instance on the Wine side in its destructor.
     */
    virtual ~YaComponent() = 0;

    DECLARE_FUNKNOWN_METHODS

    // From `IPluginBase`

    /**
     * Message to pass through a call to `IPluginBase::initialize()` to the Wine
     * plugin host. if we pass an `IHostApplication` instance, then a proxy
     * `YaHostApplication` should be created and passed as an argument to
     * `IPluginBase::initialize()`. If this is absent a null pointer should be
     * passed. The lifetime of this `YaHostApplication` object should be bound
     * to the `IComponent` we are proxying.
     */
    struct Initialize {
        using Response = UniversalTResult;

        native_size_t instance_id;
        std::optional<YaHostApplication::ConstructArgs>
            host_application_context_args;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.ext(host_application_context_args, bitsery::ext::StdOptional{});
        }
    };

    virtual tresult PLUGIN_API initialize(FUnknown* context) override = 0;

    /**
     * Message to pass through a call to `IPluginBase::terminate()` to the Wine
     * plugin host.
     */
    struct Terminate {
        using Response = UniversalTResult;

        native_size_t instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
        }
    };

    virtual tresult PLUGIN_API terminate() override = 0;

    // From `IComponent`
    tresult PLUGIN_API getControllerClassId(Steinberg::TUID classId) override;

    /**
     * Message to pass through a call to `IComponent::setIoMode(mode)` to the
     * Wine plugin host.
     */
    struct SetIoMode {
        using Response = UniversalTResult;

        native_size_t instance_id;

        Steinberg::Vst::IoMode mode;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(mode);
        }
    };

    virtual tresult PLUGIN_API
    setIoMode(Steinberg::Vst::IoMode mode) override = 0;

    /**
     * Message to pass through a call to `IComponent::getBusCount(type, dir)` to
     * the Wine plugin host.
     */
    struct GetBusCount {
        using Response = PrimitiveWrapper<int32>;

        native_size_t instance_id;

        Steinberg::Vst::BusType type;
        Steinberg::Vst::BusDirection dir;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(type);
            s.value4b(dir);
        }
    };

    virtual int32 PLUGIN_API
    getBusCount(Steinberg::Vst::MediaType type,
                Steinberg::Vst::BusDirection dir) override = 0;

    /**
     * The response code and returned bus information for a call to
     * `IComponent::getBusInfo(type, dir, index, bus <out>)`.
     */
    struct GetBusInfoResponse {
        UniversalTResult result;
        Steinberg::Vst::BusInfo updated_bus;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.object(updated_bus);
        }
    };

    /**
     * Message to pass through a call to `IComponent::getBusInfo(type, dir,
     * index, bus <out>)` to the Wine plugin host.
     */
    struct GetBusInfo {
        using Response = GetBusInfoResponse;

        native_size_t instance_id;

        Steinberg::Vst::BusType type;
        Steinberg::Vst::BusDirection dir;
        int32 index;
        Steinberg::Vst::BusInfo bus;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(type);
            s.value4b(dir);
            s.object(bus);
        }
    };

    virtual tresult PLUGIN_API
    getBusInfo(Steinberg::Vst::MediaType type,
               Steinberg::Vst::BusDirection dir,
               int32 index,
               Steinberg::Vst::BusInfo& bus /*out*/) override = 0;

    /**
     * The response code and returned routing information for a call to
     * `IComponent::getRoutingInfo(in_info, out_info <out>)`.
     */
    struct GetRoutingInfoResponse {
        UniversalTResult result;
        Steinberg::Vst::RoutingInfo updated_in_info;
        Steinberg::Vst::RoutingInfo updated_out_info;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.object(updated_in_info);
            s.object(updated_out_info);
        }
    };

    /**
     * Message to pass through a call to `IComponent::getRoutingInfo(in_info,
     * out_info <out>)` to the Wine plugin host.
     */
    struct GetRoutingInfo {
        using Response = GetRoutingInfoResponse;

        native_size_t instance_id;

        Steinberg::Vst::RoutingInfo in_info;
        Steinberg::Vst::RoutingInfo out_info;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.object(in_info);
            s.object(out_info);
        }
    };

    virtual tresult PLUGIN_API
    getRoutingInfo(Steinberg::Vst::RoutingInfo& inInfo,
                   Steinberg::Vst::RoutingInfo& outInfo /*out*/) override = 0;

    /**
     * Message to pass through a call to `IComponent::activateBus(type, dir,
     * index, state)` to the Wine plugin host.
     */
    struct ActivateBus {
        using Response = UniversalTResult;

        native_size_t instance_id;

        Steinberg::Vst::MediaType type;
        Steinberg::Vst::BusDirection dir;
        int32 index;
        TBool state;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(type);
            s.value4b(dir);
            s.value4b(index);
            s.value1b(state);
        }
    };

    virtual tresult PLUGIN_API activateBus(Steinberg::Vst::MediaType type,
                                           Steinberg::Vst::BusDirection dir,
                                           int32 index,
                                           TBool state) override = 0;

    /**
     * Message to pass through a call to `IComponent::setActive(state)` to the
     * Wine plugin host.
     */
    struct SetActive {
        using Response = UniversalTResult;

        native_size_t instance_id;

        TBool state;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value1b(state);
        }
    };

    virtual tresult PLUGIN_API setActive(TBool state) override = 0;

    /**
     * Message to pass through a call to `IComponent::setState(state)` to the
     * Wine plugin host.
     */
    struct SetState {
        using Response = UniversalTResult;

        native_size_t instance_id;

        VectorStream state;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.object(state);
        }
    };

    virtual tresult PLUGIN_API
    setState(Steinberg::IBStream* state) override = 0;

    /**
     * The response code and written state for a call to
     * `IComponent::getState(state)`.
     */
    struct GetStateResponse {
        UniversalTResult result;
        VectorStream updated_state;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.object(updated_state);
        }
    };

    /**
     * Message to pass through a call to `IComponent::getState(state)` to the
     * Wine plugin host.
     */
    struct GetState {
        using Response = GetStateResponse;

        native_size_t instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
        }
    };

    virtual tresult PLUGIN_API
    getState(Steinberg::IBStream* state) override = 0;

    // From `IAudioProcessor`

    /**
     * Message to pass through a call to
     * `IAudioProcessor::setBusArrangements(inputs, num_ins, outputs, num_outs)`
     * to the Wine plugin host.
     */
    struct SetBusArrangements {
        using Response = UniversalTResult;

        native_size_t instance_id;

        // These are orginally C-style heap arrays, not normal pointers
        std::vector<Steinberg::Vst::SpeakerArrangement> inputs;
        int32 num_ins;
        std::vector<Steinberg::Vst::SpeakerArrangement> outputs;
        int32 num_outs;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.container8b(inputs, max_num_speakers);
            s.value4b(num_ins);
            s.container8b(outputs, max_num_speakers);
            s.value4b(num_outs);
        }
    };

    virtual tresult PLUGIN_API
    setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs,
                       int32 numIns,
                       Steinberg::Vst::SpeakerArrangement* outputs,
                       int32 numOuts) override = 0;

    /**
     * The response code and written state for a call to
     * `IAudioProcessor::getBusArrangement(dir, index, arr)`.
     */
    struct GetBusArrangementResponse {
        UniversalTResult result;
        Steinberg::Vst::SpeakerArrangement updated_arr;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.value8b(updated_arr);
        }
    };

    /**
     * Message to pass through a call to
     * `IAudioProcessor::getBusArrangement(dir, index, arr)` to the Wine
     * plugin host.
     */
    struct GetBusArrangement {
        using Response = GetBusArrangementResponse;

        native_size_t instance_id;

        Steinberg::Vst::BusDirection dir;
        int32 index;
        Steinberg::Vst::SpeakerArrangement arr;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(dir);
            s.value4b(index);
            s.value8b(arr);
        }
    };

    virtual tresult PLUGIN_API
    getBusArrangement(Steinberg::Vst::BusDirection dir,
                      int32 index,
                      Steinberg::Vst::SpeakerArrangement& arr) override = 0;

    /**
     * Message to pass through a call to
     * `IAudioProcessor::canProcessSampleSize(symbolic_sample_size)` to the Wine
     * plugin host.
     */
    struct CanProcessSampleSize {
        using Response = UniversalTResult;

        native_size_t instance_id;

        int32 symbolic_sample_size;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(symbolic_sample_size);
        }
    };

    virtual tresult PLUGIN_API
    canProcessSampleSize(int32 symbolicSampleSize) override = 0;

    /**
     * Message to pass through a call to `IAudioProcessor::getLatencySamples()`
     * to the Wine plugin host.
     */
    struct GetLatencySamples {
        using Response = PrimitiveWrapper<uint32>;

        native_size_t instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
        }
    };

    virtual uint32 PLUGIN_API getLatencySamples() override = 0;

    /**
     * Message to pass through a call to
     * `IAudioProcessor::setupProcessing(setup)` to the Wine plugin host.
     */
    struct SetupProcessing {
        using Response = UniversalTResult;

        native_size_t instance_id;

        Steinberg::Vst::ProcessSetup setup;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.object(setup);
        }
    };

    virtual tresult PLUGIN_API
    setupProcessing(Steinberg::Vst::ProcessSetup& setup) override = 0;

    /**
     * Message to pass through a call to `IAudioProcessor::setProcessing(state)`
     * to the Wine plugin host.
     */
    struct SetProcessing {
        using Response = UniversalTResult;

        native_size_t instance_id;

        TBool state;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value1b(state);
        }
    };

    virtual tresult PLUGIN_API setProcessing(TBool state) override = 0;

    /**
     * The response code and all the output data resulting from a call to
     * `IAudioProcessor::process(data)`.
     */
    struct ProcessResponse {
        UniversalTResult result;
        YaProcessDataResponse output_data;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.object(output_data);
        }
    };

    /**
     * Message to pass through a call to `IAudioProcessor::process(data)` to the
     * Wine plugin host. This `YaProcessData` object wraps around all input
     * audio buffers, parameter changes and events along with all context data
     * provided by the host so we can send it to the Wine plugin host. We can
     * then use `YaProcessData::get()` on the Wine plugin host side to
     * reconstruct the original `ProcessData` object, and we then finally use
     * `YaProcessData::move_outputs_to_response()` to create a response object
     * that we can write back to the `ProcessData` object provided by the host.
     */
    struct Process {
        using Response = ProcessResponse;

        native_size_t instance_id;

        YaProcessData data;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.object(data);
        }
    };

    virtual tresult PLUGIN_API
    process(Steinberg::Vst::ProcessData& data) override = 0;

    /**
     * Message to pass through a call to `IAudioProcessor::getTailSamples()`
     * to the Wine plugin host.
     */
    struct GetTailSamples {
        using Response = PrimitiveWrapper<uint32>;

        native_size_t instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
        }
    };

    virtual uint32 PLUGIN_API getTailSamples() override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop

template <typename S>
void serialize(
    S& s,
    std::variant<YaComponent::ConstructArgs, UniversalTResult>& result) {
    s.ext(result, bitsery::ext::StdVariant{});
}

namespace Steinberg {
namespace Vst {
template <typename S>
void serialize(S& s, Steinberg::Vst::BusInfo& info) {
    s.value4b(info.mediaType);
    s.value4b(info.direction);
    s.value4b(info.channelCount);
    s.container2b(info.name);
    s.value4b(info.busType);
    s.value4b(info.flags);
}

template <typename S>
void serialize(S& s, Steinberg::Vst::RoutingInfo& info) {
    s.value4b(info.mediaType);
    s.value4b(info.busIndex);
    s.value4b(info.channel);
}

template <typename S>
void serialize(S& s, Steinberg::Vst::ProcessSetup& setup) {
    s.value4b(setup.processMode);
    s.value4b(setup.symbolicSampleSize);
    s.value4b(setup.maxSamplesPerBlock);
    s.value8b(setup.sampleRate);
}
}  // namespace Vst
}  // namespace Steinberg
