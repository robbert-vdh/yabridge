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

YaAudioBusBuffers::YaAudioBusBuffers() {}

YaAudioBusBuffers::YaAudioBusBuffers(int32 sample_size,
                                     size_t num_samples,
                                     size_t num_channels)
    : buffers(sample_size == Steinberg::Vst::SymbolicSampleSizes::kSample64
                  ? decltype(buffers)(std::vector<std::vector<double>>(
                        num_channels,
                        std::vector<double>(num_samples, 0.0)))
                  : decltype(buffers)(std::vector<std::vector<float>>(
                        num_channels,
                        std::vector<float>(num_samples, 0.0)))) {}

YaAudioBusBuffers::YaAudioBusBuffers(
    int32 sample_size,
    int32 num_samples,
    const Steinberg::Vst::AudioBusBuffers& data)
    : silence_flags(data.silenceFlags) {
    switch (sample_size) {
        case Steinberg::Vst::kSample64: {
            std::vector<std::vector<double>> vector_buffers(data.numChannels);
            for (int channel = 0; channel < data.numChannels; channel++) {
                vector_buffers[channel].assign(
                    &data.channelBuffers64[channel][0],
                    &data.channelBuffers64[channel][num_samples]);
            }

            buffers = std::move(vector_buffers);
        } break;
        case Steinberg::Vst::kSample32:
        // I don't think they'll add any other sample sizes any time soon
        default: {
            std::vector<std::vector<float>> vector_buffers(data.numChannels);
            for (int channel = 0; channel < data.numChannels; channel++) {
                vector_buffers[channel].assign(
                    &data.channelBuffers32[channel][0],
                    &data.channelBuffers32[channel][num_samples]);
            }

            buffers = std::move(vector_buffers);
        } break;
    }
}

Steinberg::Vst::AudioBusBuffers YaAudioBusBuffers::get() {
    Steinberg::Vst::AudioBusBuffers reconstructed_buffers;
    reconstructed_buffers.silenceFlags = silence_flags;
    std::visit(overload{
                   [&](std::vector<std::vector<double>>& buffers) {
                       buffer_pointers.clear();
                       for (auto& buffer : buffers) {
                           buffer_pointers.push_back(buffer.data());
                       }

                       reconstructed_buffers.numChannels =
                           static_cast<int32>(buffers.size());
                       reconstructed_buffers.channelBuffers64 =
                           reinterpret_cast<double**>(buffer_pointers.data());
                   },
                   [&](std::vector<std::vector<float>>& buffers) {
                       buffer_pointers.clear();
                       for (auto& buffer : buffers) {
                           buffer_pointers.push_back(buffer.data());
                       }

                       reconstructed_buffers.numChannels =
                           static_cast<int32>(buffers.size());
                       reconstructed_buffers.channelBuffers32 =
                           reinterpret_cast<float**>(buffer_pointers.data());
                   },
               },
               buffers);

    return reconstructed_buffers;
}

size_t YaAudioBusBuffers::num_channels() const {
    return std::visit([&](const auto& buffers) { return buffers.size(); },
                      buffers);
}

void YaAudioBusBuffers::write_back_outputs(
    Steinberg::Vst::AudioBusBuffers& output_buffers) const {
    output_buffers.silenceFlags = silence_flags;
    std::visit(
        overload{
            [&](const std::vector<std::vector<double>>& buffers) {
                for (int channel = 0; channel < output_buffers.numChannels;
                     channel++) {
                    std::copy(buffers[channel].begin(), buffers[channel].end(),
                              output_buffers.channelBuffers64[channel]);
                }
            },
            [&](const std::vector<std::vector<float>>& buffers) {
                for (int channel = 0; channel < output_buffers.numChannels;
                     channel++) {
                    std::copy(buffers[channel].begin(), buffers[channel].end(),
                              output_buffers.channelBuffers32[channel]);
                }
            },
        },
        buffers);
}

void YaProcessDataResponse::write_back_outputs(
    Steinberg::Vst::ProcessData& process_data) {
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

YaProcessData::YaProcessData() {}

YaProcessData::YaProcessData(const Steinberg::Vst::ProcessData& process_data)
    : process_mode(process_data.processMode),
      symbolic_sample_size(process_data.symbolicSampleSize),
      num_samples(process_data.numSamples),
      outputs_num_channels(process_data.numOutputs),
      // Even though `ProcessData::inputParamterChanges` is mandatory, the VST3
      // validator will pass a null pointer here
      input_parameter_changes(
          process_data.inputParameterChanges
              ? YaParameterChanges(*process_data.inputParameterChanges)
              : YaParameterChanges()),
      output_parameter_changes_supported(process_data.outputParameterChanges),
      input_events(process_data.inputEvents ? std::make_optional<YaEventList>(
                                                  *process_data.inputEvents)
                                            : std::nullopt),
      output_events_supported(process_data.outputEvents),
      process_context(process_data.processContext
                          ? std::make_optional<Steinberg::Vst::ProcessContext>(
                                *process_data.processContext)
                          : std::nullopt) {
    for (int i = 0; i < process_data.numInputs; i++) {
        inputs.emplace_back(symbolic_sample_size, num_samples,
                            process_data.inputs[i]);
    }

    // Fetch the number of channels for each output so we can recreate these
    // buffers in the Wine plugin host
    for (int i = 0; i < process_data.numOutputs; i++) {
        outputs_num_channels[i] = process_data.outputs[i].numChannels;
    }
}

Steinberg::Vst::ProcessData& YaProcessData::get() {
    // We'll have to transform out `YaAudioBusBuffers` objects into an array of
    // `AudioBusBuffers` object so the plugin can deal with them. These objects
    // contain pointers to those original objects and thus don't store any
    // buffer data themselves.
    inputs_audio_bus_buffers.clear();
    for (auto& buffers : inputs) {
        inputs_audio_bus_buffers.push_back(buffers.get());
    }

    // We'll do the same with with the outputs, but we'll first have to
    // initialize zeroed out buffers for the plugin to work with since we didn't
    // serialize those directly
    outputs.clear();
    outputs_audio_bus_buffers.clear();
    for (auto& num_channels : outputs_num_channels) {
        YaAudioBusBuffers& buffers = outputs.emplace_back(
            symbolic_sample_size, num_samples, num_channels);
        outputs_audio_bus_buffers.push_back(buffers.get());
    }

    reconstructed_process_data.processMode = process_mode;
    reconstructed_process_data.symbolicSampleSize = symbolic_sample_size;
    reconstructed_process_data.numSamples = num_samples;
    reconstructed_process_data.numInputs = static_cast<int32>(inputs.size());
    reconstructed_process_data.numOutputs =
        static_cast<int32>(outputs_num_channels.size());
    reconstructed_process_data.inputs = inputs_audio_bus_buffers.data();
    reconstructed_process_data.outputs = outputs_audio_bus_buffers.data();

    reconstructed_process_data.inputParameterChanges = &input_parameter_changes;
    if (output_parameter_changes_supported) {
        output_parameter_changes.emplace();
        reconstructed_process_data.outputParameterChanges =
            &*output_parameter_changes;
    } else {
        output_parameter_changes.reset();
        reconstructed_process_data.outputParameterChanges = nullptr;
    }

    if (input_events) {
        reconstructed_process_data.inputEvents = &*input_events;
    } else {
        reconstructed_process_data.inputEvents = nullptr;
    }

    if (output_events_supported) {
        output_events.emplace();
        reconstructed_process_data.outputEvents = &*output_events;
    } else {
        output_events.reset();
        reconstructed_process_data.outputEvents = nullptr;
    }

    if (process_context) {
        reconstructed_process_data.processContext = &*process_context;
    } else {
        reconstructed_process_data.processContext = nullptr;
    }

    return reconstructed_process_data;
}

YaProcessDataResponse YaProcessData::move_outputs_to_response() {
    // NOTE: We _have_ to manually copy over the silence flags from the
    //       `ProcessData` object generated in `get()` here sicne these of
    //       course are not references or pointers like all other fields, so
    //       they're not implicitly copied like all of our other fields
    for (int i = 0; i < reconstructed_process_data.numOutputs; i++) {
        outputs[i].silence_flags =
            reconstructed_process_data.outputs[i].silenceFlags;
    }

    return YaProcessDataResponse{
        .outputs = std::move(outputs),
        .output_parameter_changes = std::move(output_parameter_changes),
        .output_events = std::move(output_events)};
}
