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
    : response_object_{.outputs = &outputs_,
                       .output_parameter_changes = &output_parameter_changes_,
                       .output_events = &output_events_},
      // This needs to be zero initialized so we can safely call
      // `create_response()` on the plugin side
      reconstructed_process_data_() {}

void YaProcessData::repopulate(const Steinberg::Vst::ProcessData& process_data,
                               AudioShmBuffer& shared_audio_buffers) {
    // In this function and in every function we call, we should be careful to
    // not use `push_back`/`emplace_back` anywhere. Resizing vectors and
    // modifying them in place performs much better because that avoids
    // destroying and creating objects most of the time.
    process_mode_ = process_data.processMode;
    symbolic_sample_size_ = process_data.symbolicSampleSize;
    num_samples_ = process_data.numSamples;

    // The actual audio is stored in an accompanying `AudioShmBuffer` object, so
    // these inputs and outputs objects are only used to serialize metadata
    // about the input and output audio bus buffers
    inputs_.resize(process_data.numInputs);
    for (int bus = 0; bus < process_data.numInputs; bus++) {
        // NOTE: The host might provide more input channels than what the plugin
        //       asked for. Carla does this for some reason. We should just
        //       ignore these.
        inputs_[bus].numChannels = std::min(
            static_cast<int32>(shared_audio_buffers.num_input_channels(bus)),
            process_data.inputs[bus].numChannels);
        inputs_[bus].silenceFlags = process_data.inputs[bus].silenceFlags;

        // We copy the actual input audio for every bus to the shared memory
        // object
        for (int channel = 0; channel < inputs_[bus].numChannels; channel++) {
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

    outputs_.resize(process_data.numOutputs);
    for (int bus = 0; bus < process_data.numOutputs; bus++) {
        // NOTE: The host might provide more output channels than what the
        //       plugin asked for. Carla does this for some reason. We should
        //       just ignore these.
        outputs_[bus].numChannels = std::min(
            static_cast<int32>(shared_audio_buffers.num_output_channels(bus)),
            process_data.outputs[bus].numChannels);
        outputs_[bus].silenceFlags = process_data.outputs[bus].silenceFlags;
    }

    // Even though `ProcessData::inputParamterChanges` is mandatory, the VST3
    // validator will pass a null pointer here
    if (process_data.inputParameterChanges) {
        input_parameter_changes_.repopulate(
            *process_data.inputParameterChanges);
    } else {
        input_parameter_changes_.clear();
    }

    // The existence of the output parameter changes object indicates whether or
    // not the host provides this for the plugin
    if (process_data.outputParameterChanges) {
        if (!output_parameter_changes_) {
            output_parameter_changes_.emplace();
        }
    } else {
        output_parameter_changes_.reset();
    }

    if (process_data.inputEvents) {
        if (!input_events_) {
            input_events_.emplace();
        }
        input_events_->repopulate(*process_data.inputEvents);
    } else {
        input_events_.reset();
    }

    // Same for the output events
    if (process_data.outputEvents) {
        if (!output_events_) {
            output_events_.emplace();
        }
    } else {
        output_events_.reset();
    }

    if (process_data.processContext) {
        process_context_.emplace(*process_data.processContext);
    } else {
        process_context_.reset();
    }
}

Steinberg::Vst::ProcessData& YaProcessData::reconstruct(
    std::vector<std::vector<void*>>& input_pointers,
    std::vector<std::vector<void*>>& output_pointers) {
    reconstructed_process_data_.processMode = process_mode_;
    reconstructed_process_data_.symbolicSampleSize = symbolic_sample_size_;
    reconstructed_process_data_.numSamples = num_samples_;
    reconstructed_process_data_.numInputs = static_cast<int32>(inputs_.size());
    reconstructed_process_data_.numOutputs =
        static_cast<int32>(outputs_.size());

    // The actual audio data is contained within a shared memory object, and the
    // input and output pointers point to regions in that object. These pointers
    // are calculated while handling `IAudioProcessor::setActive()`.
    // NOTE: The 32-bit and 64-bit audio pointers are a union, and since this is
    //       a raw memory buffer we can set either `channelBuffers32` or
    //       `channelBuffers64` to point at that buffer as long as we do the
    //       same thing on both the native plugin side and on the Wine plugin
    //       host
    assert(inputs_.size() <= input_pointers.size() &&
           outputs_.size() <= output_pointers.size());
    for (size_t bus = 0; bus < inputs_.size(); bus++) {
        inputs_[bus].channelBuffers32 =
            reinterpret_cast<float**>(input_pointers[bus].data());
    }
    for (size_t bus = 0; bus < outputs_.size(); bus++) {
        outputs_[bus].channelBuffers32 =
            reinterpret_cast<float**>(output_pointers[bus].data());
    }

    reconstructed_process_data_.inputs = inputs_.data();
    reconstructed_process_data_.outputs = outputs_.data();

    reconstructed_process_data_.inputParameterChanges =
        &input_parameter_changes_;

    if (output_parameter_changes_) {
        output_parameter_changes_->clear();
        reconstructed_process_data_.outputParameterChanges =
            &*output_parameter_changes_;
    } else {
        reconstructed_process_data_.outputParameterChanges = nullptr;
    }

    if (input_events_) {
        reconstructed_process_data_.inputEvents = &*input_events_;
    } else {
        reconstructed_process_data_.inputEvents = nullptr;
    }

    if (output_events_) {
        output_events_->clear();
        reconstructed_process_data_.outputEvents = &*output_events_;
    } else {
        reconstructed_process_data_.outputEvents = nullptr;
    }

    if (process_context_) {
        reconstructed_process_data_.processContext = &*process_context_;
    } else {
        reconstructed_process_data_.processContext = nullptr;
    }

    return reconstructed_process_data_;
}

YaProcessData::Response& YaProcessData::create_response() noexcept {
    // NOTE: We return an object that only contains references to these original
    //       fields to avoid any copies or moves
    return response_object_;
}

void YaProcessData::write_back_outputs(
    Steinberg::Vst::ProcessData& process_data,
    const AudioShmBuffer& shared_audio_buffers) {
    assert(static_cast<int32>(outputs_.size()) == process_data.numOutputs);
    for (int bus = 0; bus < process_data.numOutputs; bus++) {
        process_data.outputs[bus].silenceFlags = outputs_[bus].silenceFlags;

        // NOTE: Some hosts, like Carla, provide more output channels than what
        //       the plugin wants. We'll have already capped
        //       `outputs[bus].numChannels` to the number of channels requested
        //       by the plugin during `YaProcessData::repopulate()`.
        for (int channel = 0; channel < outputs_[bus].numChannels; channel++) {
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

    if (output_parameter_changes_ && process_data.outputParameterChanges) {
        output_parameter_changes_->write_back_outputs(
            *process_data.outputParameterChanges);
    }

    if (output_events_ && process_data.outputEvents) {
        output_events_->write_back_outputs(*process_data.outputEvents);
    }
}
