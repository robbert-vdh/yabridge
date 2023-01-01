// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2023 Robbert van der Helm
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more destates.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "process.h"

namespace clap {
namespace process {

Process::Process() noexcept {}

void Process::repopulate(const clap_process_t& process,
                         AudioShmBuffer& shared_audio_buffers) {
    assert(process.in_events && process.out_events);
    if (process.audio_inputs_count > 0) {
        assert(process.audio_inputs);
    }
    if (process.audio_outputs_count > 0) {
        assert(process.audio_outputs);
    }

    // In this function and in every function we call, we should be careful to
    // not use `push_back`/`emplace_back` anywhere. Resizing vectors and
    // modifying them in place performs much better because that avoids
    // destroying and creating objects most of the time.
    steady_time_ = process.steady_time;
    frames_count_ = process.frames_count;

    if (process.transport) {
        transport_.emplace(*process.transport);
    } else {
        transport_.reset();
    }

    // The actual audio is stored in an accompanying `AudioShmBuffer` object, so
    // these inputs and outputs objects are only used to serialize metadata
    // about the input and output audio bus buffers
    audio_inputs_.resize(process.audio_inputs_count);
    audio_inputs_type_.resize(process.audio_inputs_count);
    for (size_t port = 0; port < process.audio_inputs_count; port++) {
        // NOTE: With VST3 plugins sometimes hosts provided more ports than the
        //       plugin asked for (or sometimes fewer, fun). So we'll account
        //       for both cases just to be safe.
        audio_inputs_[port].channel_count =
            std::min(static_cast<uint32_t>(
                         shared_audio_buffers.num_input_channels(port)),
                     process.audio_inputs[port].channel_count);
        audio_inputs_[port].latency = process.audio_inputs[port].latency;
        audio_inputs_[port].constant_mask =
            process.audio_inputs[port].constant_mask;

        // We'll encode the port type using a separate vector because we can't
        // store it in place without creating dangling pointers
        if (process.audio_inputs[port].data32) {
            audio_inputs_type_[port] =
                clap::audio_buffer::AudioBufferType::Float32;

            // We copy the actual input audio for every bus to the shared memory
            // object
            for (uint32_t channel = 0;
                 channel < process.audio_inputs[port].channel_count;
                 channel++) {
                std::copy_n(process.audio_inputs[port].data32[channel],
                            frames_count_,
                            shared_audio_buffers.input_channel_ptr<float>(
                                port, channel));
            }
        } else if (process.audio_inputs[port].data64) {
            audio_inputs_type_[port] =
                clap::audio_buffer::AudioBufferType::Double64;

            for (uint32_t channel = 0;
                 channel < process.audio_inputs[port].channel_count;
                 channel++) {
                std::copy_n(process.audio_inputs[port].data64[channel],
                            frames_count_,
                            shared_audio_buffers.input_channel_ptr<double>(
                                port, channel));
            }
        } else {
            // Only reasonable-ish (it's still not reasonable) time where
            // neither of the pointers is set
            assert(process.audio_inputs[port].channel_count == 0);
        }
    }

    audio_outputs_.resize(process.audio_outputs_count);
    audio_outputs_type_.resize(process.audio_outputs_count);
    for (size_t port = 0; port < process.audio_outputs_count; port++) {
        // The same notes apply to the outputs
        audio_outputs_[port].channel_count =
            std::min(static_cast<uint32_t>(
                         shared_audio_buffers.num_output_channels(port)),
                     process.audio_outputs[port].channel_count);
        audio_outputs_[port].latency = process.audio_outputs[port].latency;
        // Shouldn't be any reason to bridge this, but who knows what will
        // happen when we don't
        audio_outputs_[port].constant_mask =
            process.audio_outputs[port].constant_mask;

        if (process.audio_outputs[port].data32) {
            audio_outputs_type_[port] =
                clap::audio_buffer::AudioBufferType::Float32;
        } else if (process.audio_outputs[port].data64) {
            audio_outputs_type_[port] =
                clap::audio_buffer::AudioBufferType::Double64;
        } else {
            // Only reasonable-ish (it's still not reasonable) time where
            // neither of the pointers is set
            assert(process.audio_outputs[port].channel_count == 0);
        }
    }

    in_events_.repopulate(*process.in_events);
}

const clap_process_t& Process::reconstruct(
    std::vector<std::vector<void*>>& input_pointers,
    std::vector<std::vector<void*>>& output_pointers) {
    reconstructed_process_data_.steady_time = steady_time_;
    reconstructed_process_data_.frames_count = frames_count_;
    reconstructed_process_data_.transport = transport_ ? &*transport_ : nullptr;

    // The actual audio data is contained within a shared memory object, and the
    // input and output pointers point to regions in that object. These pointers
    // are calculated during `clap_plugin::activate()`.
    assert(audio_inputs_.size() <= input_pointers.size() &&
           audio_outputs_.size() <= output_pointers.size() &&
           audio_inputs_type_.size() == audio_inputs_.size() &&
           audio_outputs_type_.size() == audio_outputs_.size());
    for (size_t port = 0; port < audio_inputs_.size(); port++) {
        // The sample depth depends on whether the plugin claimed to support
        // 64-bit or not and whether the host ended up passing us 32-bit or
        // 64-bit audio
        switch (audio_inputs_type_[port]) {
            case clap::audio_buffer::AudioBufferType::Float32:
            default:
                audio_inputs_[port].data32 =
                    reinterpret_cast<float**>(input_pointers[port].data());
                break;
            case clap::audio_buffer::AudioBufferType::Double64:
                audio_inputs_[port].data64 =
                    reinterpret_cast<double**>(input_pointers[port].data());
                break;
        }
    }
    for (size_t port = 0; port < audio_outputs_.size(); port++) {
        switch (audio_outputs_type_[port]) {
            case clap::audio_buffer::AudioBufferType::Float32:
            default:
                audio_outputs_[port].data32 =
                    reinterpret_cast<float**>(output_pointers[port].data());
                break;
            case clap::audio_buffer::AudioBufferType::Double64:
                audio_outputs_[port].data64 =
                    reinterpret_cast<double**>(output_pointers[port].data());
                break;
        }
    }

    reconstructed_process_data_.audio_inputs = audio_inputs_.data();
    reconstructed_process_data_.audio_outputs = audio_outputs_.data();
    reconstructed_process_data_.audio_inputs_count =
        static_cast<uint32_t>(audio_inputs_.size());
    reconstructed_process_data_.audio_outputs_count =
        static_cast<uint32_t>(audio_outputs_.size());

    out_events_.clear();
    reconstructed_process_data_.in_events = in_events_.input_events();
    reconstructed_process_data_.out_events = out_events_.output_events();

    return reconstructed_process_data_;
}

Process::Response& Process::create_response() noexcept {
    // This response object acts as an optimization. It stores pointers to the
    // original fields in our objects, so we can both only serialize those
    // fields when sending the response from the Wine side. This lets us avoid
    // allocations by not having to copy or move the data. On the plugin side we
    // need to be careful to deserialize into an existing
    // `clap::plugin::ProcessResponse` object with a response object that
    // belongs to an actual process data object, because with these changes it's
    // no longer possible to deserialize those results into a new ad-hoc created
    // object.
    response_object_.audio_outputs = &audio_outputs_;
    response_object_.out_events = &out_events_;

    return response_object_;
}

void Process::write_back_outputs(const clap_process_t& process,
                                 const AudioShmBuffer& shared_audio_buffers) {
    assert(process.audio_outputs && process.out_events);

    assert(audio_outputs_.size() == process.audio_outputs_count);
    for (size_t port = 0; port < audio_outputs_.size(); port++) {
        process.audio_outputs[port].constant_mask =
            audio_outputs_[port].constant_mask;
        // Don't think the plugin is supposed to change this, but uh may as well
        process.audio_outputs[port].latency = audio_outputs_[port].latency;

        // `audio_outputs_[port].channel_count` is the minimum of the plugin's
        // and the host's channel count
        for (size_t channel = 0; channel < audio_outputs_[port].channel_count;
             channel++) {
            // We copy the output audio for every bus from the shared memory
            // object back to the buffer provided by the host
            switch (audio_outputs_type_[port]) {
                case clap::audio_buffer::AudioBufferType::Float32:
                default:
                    std::copy_n(shared_audio_buffers.output_channel_ptr<float>(
                                    port, channel),
                                process.frames_count,
                                process.audio_outputs[port].data32[channel]);
                    break;
                case clap::audio_buffer::AudioBufferType::Double64:
                    std::copy_n(shared_audio_buffers.output_channel_ptr<double>(
                                    port, channel),
                                process.frames_count,
                                process.audio_outputs[port].data64[channel]);
                    break;
            }
        }
    }

    out_events_.write_back_outputs(*process.out_events);
}

}  // namespace process
}  // namespace clap
