// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2022 Robbert van der Helm
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

#include <pluginterfaces/vst/ivstaudioprocessor.h>

#include "../../../audio-shm.h"
#include "../../../bitsery/ext/in-place-optional.h"
#include "../../common.h"
#include "../base.h"
#include "../process-data.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IAudioProcessor` for serialization purposes. This is
 * instantiated as part of `Vst3PluginProxy`.
 */
class YaAudioProcessor : public Steinberg::Vst::IAudioProcessor {
   public:
    /**
     * These are the arguments for creating a `YaAudioProcessor`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements `IAudioProcessor`
         * and read arguments from it.
         */
        ConstructArgs(Steinberg::IPtr<Steinberg::FUnknown> object) noexcept;

        /**
         * Whether the object supported this interface.
         */
        bool supported;

        template <typename S>
        void serialize(S& s) {
            s.value1b(supported);
        }
    };

    /**
     * Instantiate this instance with arguments read from another interface
     * implementation.
     */
    YaAudioProcessor(ConstructArgs&& args) noexcept;

    virtual ~YaAudioProcessor() noexcept = default;

    inline bool supported() const noexcept { return arguments_.supported; }

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
     * `IAudioProcessor::getBusArrangement(dir, index, &arr)`.
     */
    struct GetBusArrangementResponse {
        UniversalTResult result;
        Steinberg::Vst::SpeakerArrangement arr;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.value8b(arr);
        }
    };

    /**
     * Message to pass through a call to
     * `IAudioProcessor::getBusArrangement(dir, index, &arr)` to the Wine
     * plugin host.
     */
    struct GetBusArrangement {
        using Response = GetBusArrangementResponse;

        native_size_t instance_id;

        Steinberg::Vst::BusDirection dir;
        int32 index;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(dir);
            s.value4b(index);
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
        using Response = PrimitiveResponse<uint32>;

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
        YaProcessData::Response output_data;

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
     * then use `YaProcessData::reconstruct()` on the Wine plugin host side to
     * reconstruct the original `ProcessData` object, and we then finally use
     * `YaProcessData::create_response()` to create a response object that we
     * can write the plugin's changes back to the `ProcessData` object provided
     * by the host.
     */
    struct Process {
        using Response = ProcessResponse;

        native_size_t instance_id;

        YaProcessData data;

        /**
         * We'll periodically synchronize the realtime priority setting of the
         * host's audio thread with the Wine plugin host. We'll do this
         * approximately every ten seconds, as doing this getting and setting
         * scheduler information has a non trivial amount of overhead (even if
         * it's only a single microsoecond).
         */
        std::optional<int> new_realtime_priority;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.object(data);

            s.ext(new_realtime_priority, bitsery::ext::InPlaceOptional{},
                  [](S& s, int& priority) { s.value4b(priority); });
        }
    };

    virtual tresult PLUGIN_API
    process(Steinberg::Vst::ProcessData& data) override = 0;

    /**
     * Message to pass through a call to `IAudioProcessor::getTailSamples()`
     * to the Wine plugin host.
     */
    struct GetTailSamples {
        using Response = PrimitiveResponse<uint32>;

        native_size_t instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
        }
    };

    virtual uint32 PLUGIN_API getTailSamples() override = 0;

   protected:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop

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
