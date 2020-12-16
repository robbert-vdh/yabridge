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

#include "process-data.h"

#include "src/common/utils.h"

YaAudioBusBuffers::YaAudioBusBuffers() {}

YaAudioBusBuffers::YaAudioBusBuffers(int32 sample_size,
                                     size_t num_channels,
                                     size_t num_samples)
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

Steinberg::Vst::AudioBusBuffers& YaAudioBusBuffers::get() {
    reconstructed_buffers.silenceFlags = silence_flags;
    std::visit(overload{
                   [&](std::vector<std::vector<double>>& buffers) {
                       buffer_pointers.clear();
                       for (auto& buffer : buffers) {
                           buffer_pointers.push_back(buffer.data());
                       }

                       reconstructed_buffers.numChannels = buffers.size();
                       reconstructed_buffers.channelBuffers64 =
                           reinterpret_cast<double**>(buffer_pointers.data());
                   },
                   [&](std::vector<std::vector<float>>& buffers) {
                       buffer_pointers.clear();
                       for (auto& buffer : buffers) {
                           buffer_pointers.push_back(buffer.data());
                       }

                       reconstructed_buffers.numChannels = buffers.size();
                       reconstructed_buffers.channelBuffers32 =
                           reinterpret_cast<float**>(buffer_pointers.data());
                   },
               },
               buffers);

    return reconstructed_buffers;
}

YaProcessData::YaProcessData() {}

YaProcessData::YaProcessData(const Steinberg::Vst::ProcessData& process_data)
    : process_mode(process_data.processMode),
      symbolic_sample_size(process_data.symbolicSampleSize),
      num_samples(process_data.numSamples),
      outputs_num_channels(process_data.numOutputs),
      input_parameter_changes(*process_data.inputParameterChanges),
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

// TODO: Reconstruction
