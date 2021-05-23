// yabridge: a Wine VST bridge
// Copyright (C) 2020-2021 Robbert van der Helm
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

#include "src/common/utils.h"

YaAudioBusBuffers::YaAudioBusBuffers() noexcept {}

void YaAudioBusBuffers::clear(int32 sample_size,
                              size_t num_samples,
                              size_t num_channels) {
    auto do_clear = [&]<typename T>(T) {
        if (!std::holds_alternative<std::vector<std::vector<T>>>(buffers)) {
            buffers.emplace<std::vector<std::vector<T>>>();
        }

        std::vector<std::vector<T>>& vector_buffers =
            std::get<std::vector<std::vector<T>>>(buffers);
        vector_buffers.resize(num_channels);
        for (size_t i = 0; i < vector_buffers.size(); i++) {
            vector_buffers[i].resize(num_samples);
        }
    };

    if (sample_size == Steinberg::Vst::SymbolicSampleSizes::kSample64) {
        // XXX: Clangd doesn't let you specify template parameters for templated
        //      lambdas. This argument should get optimized out
        do_clear(double());
    } else {
        do_clear(float());
    }
}

void YaAudioBusBuffers::repopulate(
    int32 sample_size,
    int32 num_samples,
    const Steinberg::Vst::AudioBusBuffers& data) {
    silence_flags = data.silenceFlags;

    auto do_repopuldate = [&]<typename T>(T** original_buffer) {
        if (!std::holds_alternative<std::vector<std::vector<T>>>(buffers)) {
            buffers.emplace<std::vector<std::vector<T>>>();
        }

        std::vector<std::vector<T>>& vector_buffers =
            std::get<std::vector<std::vector<T>>>(buffers);
        vector_buffers.resize(data.numChannels);
        for (int channel = 0; channel < data.numChannels; channel++) {
            vector_buffers[channel].assign(
                &original_buffer[channel][0],
                &original_buffer[channel][num_samples]);
        }
    };

    if (sample_size == Steinberg::Vst::kSample64) {
        do_repopuldate(data.channelBuffers64);
    } else {
        // I don't think they'll add any other sample sizes any time soon
        do_repopuldate(data.channelBuffers32);
    }
}

void YaAudioBusBuffers::reconstruct(
    Steinberg::Vst::AudioBusBuffers& reconstructed_buffers) {
    // We'll update the `AudioBusBuffers` object in place to point to our new
    // data
    reconstructed_buffers.silenceFlags = silence_flags;

    std::visit(
        [&]<typename T>(std::vector<std::vector<T>>& buffers) {
            buffer_pointers.resize(buffers.size());
            for (size_t i = 0; i < buffers.size(); i++) {
                buffer_pointers[i] = buffers[i].data();
            }

            reconstructed_buffers.numChannels =
                static_cast<int32>(buffers.size());
            if constexpr (std::is_same_v<T, double>) {
                reconstructed_buffers.channelBuffers64 =
                    reinterpret_cast<T**>(buffer_pointers.data());
            } else {
                reconstructed_buffers.channelBuffers32 =
                    reinterpret_cast<T**>(buffer_pointers.data());
            }
        },
        buffers);
}

size_t YaAudioBusBuffers::num_channels() const {
    return std::visit([&](const auto& buffers) { return buffers.size(); },
                      buffers);
}

void YaAudioBusBuffers::write_back_outputs(
    Steinberg::Vst::AudioBusBuffers& output_buffers) const {
    output_buffers.silenceFlags = silence_flags;

    std::visit(
        [&]<typename T>(const std::vector<std::vector<T>>& buffers) {
            for (int channel = 0; channel < output_buffers.numChannels;
                 channel++) {
                if constexpr (std::is_same_v<T, double>) {
                    std::copy(buffers[channel].begin(), buffers[channel].end(),
                              output_buffers.channelBuffers64[channel]);
                } else {
                    std::copy(buffers[channel].begin(), buffers[channel].end(),
                              output_buffers.channelBuffers32[channel]);
                }
            }
        },
        buffers);
}

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

void YaProcessData::repopulate(
    const Steinberg::Vst::ProcessData& process_data) {
    // In this function and in every function we call, we should be careful to
    // not use `push_back`/`emplace_back` anywhere. Resizing vectors and
    // modifying them in place performs much better because that avoids
    // destroying and creating objects most of the time.
    process_mode = process_data.processMode;
    symbolic_sample_size = process_data.symbolicSampleSize;
    num_samples = process_data.numSamples;

    // We'll make sure to not do any allocations here after the first processing
    // cycle
    inputs.resize(process_data.numInputs);
    for (int i = 0; i < process_data.numInputs; i++) {
        inputs[i].repopulate(symbolic_sample_size, num_samples,
                             process_data.inputs[i]);
    }

    // We only store how many channels ouch output has so we can recreate the
    // objects on the Wine side
    outputs_num_channels.resize(process_data.numOutputs);
    for (int i = 0; i < process_data.numOutputs; i++) {
        outputs_num_channels[i] = process_data.outputs[i].numChannels;
    }

    // Even though `ProcessData::inputParamterChanges` is mandatory, the VST3
    // validator will pass a null pointer here
    if (process_data.inputParameterChanges) {
        input_parameter_changes.repopulate(*process_data.inputParameterChanges);
    } else {
        input_parameter_changes.clear();
    }

    output_parameter_changes_supported = process_data.outputParameterChanges;

    if (process_data.inputEvents) {
        if (!input_events) {
            input_events.emplace();
        }
        input_events->repopulate(*process_data.inputEvents);
    } else {
        input_events.reset();
    }

    output_events_supported = process_data.outputEvents;

    if (process_data.processContext) {
        process_context.emplace(*process_data.processContext);
    } else {
        process_context.reset();
    }
}

Steinberg::Vst::ProcessData& YaProcessData::reconstruct() {
    reconstructed_process_data.processMode = process_mode;
    reconstructed_process_data.symbolicSampleSize = symbolic_sample_size;
    reconstructed_process_data.numSamples = num_samples;
    reconstructed_process_data.numInputs = static_cast<int32>(inputs.size());
    reconstructed_process_data.numOutputs =
        static_cast<int32>(outputs_num_channels.size());

    // We'll have to transform our `YaAudioBusBuffers` objects into an array of
    // `AudioBusBuffers` object so the plugin can deal with them. These objects
    // contain pointers to those original objects and thus don't store any
    // buffer data themselves.
    inputs_audio_bus_buffers.resize(inputs.size());
    for (size_t i = 0; i < inputs.size(); i++) {
        inputs[i].reconstruct(inputs_audio_bus_buffers[i]);
    }

    reconstructed_process_data.inputs = inputs_audio_bus_buffers.data();

    // We'll do the same with with the outputs, but we'll first have to
    // initialize zeroed out buffers for the plugin to work with since we didn't
    // serialize those directly
    outputs.resize(outputs_num_channels.size());
    outputs_audio_bus_buffers.resize(outputs_num_channels.size());
    for (size_t i = 0; i < outputs_num_channels.size(); i++) {
        outputs[i].clear(symbolic_sample_size, num_samples,
                         outputs_num_channels[i]);
        outputs[i].reconstruct(outputs_audio_bus_buffers[i]);
    }

    reconstructed_process_data.outputs = outputs_audio_bus_buffers.data();
    reconstructed_process_data.inputParameterChanges = &input_parameter_changes;

    if (output_parameter_changes_supported) {
        if (!output_parameter_changes) {
            output_parameter_changes.emplace();
        }
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

    if (output_events_supported) {
        if (!output_events) {
            output_events.emplace();
        }
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
    // NOTE: We _have_ to manually copy over the silence flags from the
    //       `ProcessData` object generated in `get()` here sicne these of
    //       course are not references or pointers like all other fields, so
    //       they're not implicitly copied like all of our other fields
    //
    //       On the plugin side this is not necessary, but it also doesn't hurt
    for (int i = 0; i < reconstructed_process_data.numOutputs; i++) {
        outputs[i].silence_flags =
            reconstructed_process_data.outputs[i].silenceFlags;
    }

    // NOTE: We return an object that only contains references to these original
    //       fields to avoid any copies or moves
    return response_object;
}

void YaProcessData::write_back_outputs(
    Steinberg::Vst::ProcessData& process_data) {
    assert(static_cast<int32>(outputs.size()) == process_data.numOutputs);
    for (int i = 0; i < process_data.numOutputs; i++) {
        outputs[i].write_back_outputs(process_data.outputs[i]);
    }

    if (output_parameter_changes && process_data.outputParameterChanges) {
        output_parameter_changes->write_back_outputs(
            *process_data.outputParameterChanges);
    }

    if (output_events && process_data.outputEvents) {
        output_events->write_back_outputs(*process_data.outputEvents);
    }
}
