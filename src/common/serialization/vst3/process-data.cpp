// yabridge: a Wine VST bridge
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

#include "process-data.h"

#include "../../utils.h"

YaProcessData::YaProcessData() noexcept
    // This response object acts as an optimization. It stores pointers to the
    // original fields in our objects, so we can both only serialize those
    // fields when sending the response from the Wine side. This lets us avoid
    // allocations by not having to copy or move the data. On the plugin side we
    // need to be careful to deserialize into an existing
    // `YaAudioProcessor::ProcessResponse` object with a response object that
    // belongs to an actual process data object, because with these changes it's
    // no longer possible to deserialize those results into a new ad-hoc created
    // object.
    : response_object{.outputs = &outputs,
                      .output_parameter_changes = &output_parameter_changes,
                      .output_events = &output_events},
      // This needs to be zero initialized so we can safely call
      // `create_response()` on the plugin side
      reconstructed_process_data() {}

void YaProcessData::repopulate(const Steinberg::Vst::ProcessData& process_data,
                               AudioShmBuffer& shared_audio_buffers) {
    // In this function and in every function we call, we should be careful to
    // not use `push_back`/`emplace_back` anywhere. Resizing vectors and
    // modifying them in place performs much better because that avoids
    // destroying and creating objects most of the time.
    process_mode = process_data.processMode;
    symbolic_sample_size = process_data.symbolicSampleSize;
    num_samples = process_data.numSamples;

    // The actual audio is stored in an accompanying `AudioShmBuffer` object, so
    // these inputs and outputs objects are only used to serialize metadata
    // about the input and output audio bus buffers
    inputs.resize(process_data.numInputs);
    for (int bus = 0; bus < process_data.numInputs; bus++) {
        // NOTE: The host might provide more input channels than what the plugin
        //       asked for. Carla does this for some reason. We should just
        //       ignore these.
        inputs[bus].numChannels = std::min(
            static_cast<int32>(shared_audio_buffers.num_input_channels(bus)),
            process_data.inputs[bus].numChannels);
        inputs[bus].silenceFlags = process_data.inputs[bus].silenceFlags;

        // We copy the actual input audio for every bus to the shared memory
        // object
        for (int channel = 0; channel < inputs[bus].numChannels; channel++) {
            if (process_data.symbolicSampleSize == Steinberg::Vst::kSample64) {
                std::copy_n(process_data.inputs[bus].channelBuffers64[channel],
                            process_data.numSamples,
                            shared_audio_buffers.input_channel_ptr<double>(
                                bus, channel));
            } else {
                std::copy_n(process_data.inputs[bus].channelBuffers32[channel],
                            process_data.numSamples,
                            shared_audio_buffers.input_channel_ptr<float>(
                                bus, channel));
            }
        }
    }

    outputs.resize(process_data.numOutputs);
    for (int bus = 0; bus < process_data.numOutputs; bus++) {
        // NOTE: The host might provide more output channels than what the
        //       plugin asked for. Carla does this for some reason. We should
        //       just ignore these.
        outputs[bus].numChannels = std::min(
            static_cast<int32>(shared_audio_buffers.num_output_channels(bus)),
            process_data.outputs[bus].numChannels);
        outputs[bus].silenceFlags = process_data.outputs[bus].silenceFlags;
    }

    // Even though `ProcessData::inputParamterChanges` is mandatory, the VST3
    // validator will pass a null pointer here
    if (process_data.inputParameterChanges) {
        input_parameter_changes.repopulate(*process_data.inputParameterChanges);
    } else {
        input_parameter_changes.clear();
    }

    // The existence of the output parameter changes object indicates whether or
    // not the host provides this for the plugin
    if (process_data.outputParameterChanges) {
        if (!output_parameter_changes) {
            output_parameter_changes.emplace();
        }
    } else {
        output_parameter_changes.reset();
    }

    if (process_data.inputEvents) {
        if (!input_events) {
            input_events.emplace();
        }
        input_events->repopulate(*process_data.inputEvents);
    } else {
        input_events.reset();
    }

    // Same for the output events
    if (process_data.outputEvents) {
        if (!output_events) {
            output_events.emplace();
        }
    } else {
        output_events.reset();
    }

    if (process_data.processContext) {
        process_context.emplace(*process_data.processContext);
    } else {
        process_context.reset();
    }
}

Steinberg::Vst::ProcessData& YaProcessData::reconstruct(
    std::vector<std::vector<void*>>& input_pointers,
    std::vector<std::vector<void*>>& output_pointers) {
    reconstructed_process_data.processMode = process_mode;
    reconstructed_process_data.symbolicSampleSize = symbolic_sample_size;
    reconstructed_process_data.numSamples = num_samples;
    reconstructed_process_data.numInputs = static_cast<int32>(inputs.size());
    reconstructed_process_data.numOutputs = static_cast<int32>(outputs.size());

    // The actual audio data is contained within a shared memory object, and the
    // input and output pointers point to regions in that object. These pointers
    // are calculated while handling `IAudioProcessor::setupProcessing()`.
    // NOTE: The 32-bit and 64-bit audio pointers are a union, and since this is
    //       a raw memory buffer we can set either `channelBuffers32` or
    //       `channelBuffers64` to point at that buffer as long as we do the
    //       same thing on both the native plugin side and on the Wine plugin
    //       host
    assert(inputs.size() <= input_pointers.size() &&
           outputs.size() <= output_pointers.size());
    for (size_t bus = 0; bus < inputs.size(); bus++) {
        inputs[bus].channelBuffers32 =
            reinterpret_cast<float**>(input_pointers[bus].data());
    }
    for (size_t bus = 0; bus < outputs.size(); bus++) {
        outputs[bus].channelBuffers32 =
            reinterpret_cast<float**>(output_pointers[bus].data());
    }

    reconstructed_process_data.inputs = inputs.data();
    reconstructed_process_data.outputs = outputs.data();

    reconstructed_process_data.inputParameterChanges = &input_parameter_changes;

    if (output_parameter_changes) {
        output_parameter_changes->clear();
        reconstructed_process_data.outputParameterChanges =
            &*output_parameter_changes;
    } else {
        reconstructed_process_data.outputParameterChanges = nullptr;
    }

    if (input_events) {
        reconstructed_process_data.inputEvents = &*input_events;
    } else {
        reconstructed_process_data.inputEvents = nullptr;
    }

    if (output_events) {
        output_events->clear();
        reconstructed_process_data.outputEvents = &*output_events;
    } else {
        reconstructed_process_data.outputEvents = nullptr;
    }

    if (process_context) {
        reconstructed_process_data.processContext = &*process_context;
    } else {
        reconstructed_process_data.processContext = nullptr;
    }

    return reconstructed_process_data;
}

YaProcessData::Response& YaProcessData::create_response() noexcept {
    // NOTE: We return an object that only contains references to these original
    //       fields to avoid any copies or moves
    return response_object;
}

void YaProcessData::write_back_outputs(
    Steinberg::Vst::ProcessData& process_data,
    const AudioShmBuffer& shared_audio_buffers) {
    assert(static_cast<int32>(outputs.size()) == process_data.numOutputs);
    for (int bus = 0; bus < process_data.numOutputs; bus++) {
        process_data.outputs[bus].silenceFlags = outputs[bus].silenceFlags;

        // NOTE: Some hosts, like Carla, provide more output channels than what
        //       the plugin wants. We'll have already capped
        //       `outputs[bus].numChannels` to the number of channels requested
        //       by the plugin during `YaProcessData::repopulate()`.
        for (int channel = 0; channel < outputs[bus].numChannels; channel++) {
            // We copy the output audio for every bus from the shared memory
            // object back to the buffer provided by the host
            if (process_data.symbolicSampleSize == Steinberg::Vst::kSample64) {
                std::copy_n(
                    shared_audio_buffers.output_channel_ptr<double>(bus,
                                                                    channel),
                    process_data.numSamples,
                    process_data.outputs[bus].channelBuffers64[channel]);
            } else {
                std::copy_n(
                    shared_audio_buffers.output_channel_ptr<float>(bus,
                                                                   channel),
                    process_data.numSamples,
                    process_data.outputs[bus].channelBuffers32[channel]);
            }
        }
    }

    if (output_parameter_changes && process_data.outputParameterChanges) {
        output_parameter_changes->write_back_outputs(
            *process_data.outputParameterChanges);
    }

    if (output_events && process_data.outputEvents) {
        output_events->write_back_outputs(*process_data.outputEvents);
    }
}
