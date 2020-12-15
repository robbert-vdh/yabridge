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

YaAudioBusBuffers::YaAudioBusBuffers(
    Steinberg::Vst::SymbolicSampleSizes sample_size,
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
    Steinberg::Vst::SymbolicSampleSizes sample_size,
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
                       double_buffer_pointers.clear();
                       for (auto& buffer : buffers) {
                           double_buffer_pointers.push_back(buffer.data());
                       }

                       reconstructed_buffers.numChannels = buffers.size();
                       reconstructed_buffers.channelBuffers64 =
                           double_buffer_pointers.data();
                   },
                   [&](std::vector<std::vector<float>>& buffers) {
                       float_buffer_pointers.clear();
                       for (auto& buffer : buffers) {
                           float_buffer_pointers.push_back(buffer.data());
                       }

                       reconstructed_buffers.numChannels = buffers.size();
                       reconstructed_buffers.channelBuffers32 =
                           float_buffer_pointers.data();
                   },
               },
               buffers);

    return reconstructed_buffers;
}
