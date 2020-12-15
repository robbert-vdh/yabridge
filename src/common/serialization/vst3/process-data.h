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

#include <variant>

#include <bitsery/ext/std_variant.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>

#include "base.h"
#include "parameter-changes.h"

// This header provides serialization wrappers around `ProcessData`

/**
 * A serializable wrapper around `AudioBusBuffers` back by `std::vector<T>`s.
 * Data can be read from a `AudioBusBuffers` object provided by the host, and
 * one the Wine plugin host side we can reconstruct the `AudioBusBuffers` object
 * back from this object again.
 *
 * @see YaProcessData
 */
class YaAudioBusBuffers {
   public:
    /**
     * A default constructor does not make any sense here since the actual data
     * is a union, but we need a default constructor for bitsery.
     */
    YaAudioBusBuffers();

    /**
     * Create a new, zero initialize audio bus buffers object. Used to
     * reconstruct the output buffers during `YaProcessData::get()`.
     */
    YaAudioBusBuffers(Steinberg::Vst::SymbolicSampleSizes sample_size,
                      size_t num_channels,
                      size_t num_samples);

    /**
     * Copy data from a host provided `AudioBusBuffers` object during a process
     * call. Constructed as part of `YaProcessData`. Since `AudioBusBuffers`
     * contains an untagged union for storing single and double precision
     * floating point values, the original `ProcessData`'s `symbolicSampleSize`
     * field determines which variant of that union to use.
     */
    YaAudioBusBuffers(Steinberg::Vst::SymbolicSampleSizes sample_size,
                      const Steinberg::Vst::AudioBusBuffers& data);

    /**
     * Reconstruct the original `AudioBusBuffers` object passed to the
     * constructor and return it. This is used as part of
     * `YaProcessData::get()`.
     */
    Steinberg::Vst::AudioBusBuffers& get();

    template <typename S>
    void serialize(S& s) {
        s.value8b(silence_flags);
        s.ext(buffers, bitsery::ext::StdVariant{
                           [](S& s, std::vector<std::vector<float>>& buffers) {
                               s.container(buffers, max_num_speakers,
                                           [](S& s, auto& channel) {
                                               s.container4b(channel, 1 << 16);
                                           });
                           },
                           [](S& s, std::vector<std::vector<double>>& buffers) {
                               s.container(buffers, max_num_speakers,
                                           [](S& s, auto& channel) {
                                               s.container8b(channel, 1 << 16);
                                           });
                           },
                       });
    }

   private:
    /**
     * The `AudioBusBuffers` object we reconstruct during `get()`.
     */
    Steinberg::Vst::AudioBusBuffers reconstructed_buffers;

    /**
     * A bitfield for silent channels copied directly from the input struct.
     */
    uint64 silence_flags;

    /**
     * The original implementation uses heap arrays and it stores a
     * {float,double} array pointer per channel, with a separate field for the
     * number of channels. We'll store this using a vector of vectors.
     */
    std::variant<std::vector<std::vector<float>>,
                 std::vector<std::vector<double>>>
        buffers;
};

/**
 * A serializable wrapper around the output fields of `ProcessData`. We send
 * this back as a response to a process call so we can write those fields back
 * to the host. It would be possible to just send `YaProcessData` back and have
 * everything be in a single structure, but that would involve a lot of
 * unnecessary copying (since, at least in theory, all the input audio buffers,
 * events and context data shouldn't have been changed by the plugin).
 *
 * @see YaProcessData
 */
struct YaProcessDataResponse {
    std::vector<YaAudioBusBuffers> outputs;
    // TODO: Don't forget to check for null before writing these back
    YaParameterChanges output_parameter_changes;

    // TODO: Add events

    template <typename S>
    void serialize(S& s) {
        s.container(outputs, max_num_speakers);
        s.container(output_parameter_changes, 1 << 16);
    }
};

/**
 * A serializable wrapper around `ProcessData`. We'll read all information from
 * the host so we can serialize it and provide an equivalent `ProcessData`
 * struct to the plugin. Then we can create a `YaProcessDataResponse` object
 * that contains all output values so we can write those back to the host.
 */
class YaProcessData {
   public:
    YaProcessData();

    /**
     * Copy data from a host provided `ProcessData` object during a process
     * call. This struct can then be serialized, and `YaProcessData::get()` can
     * then be used again to recreate the original `ProcessData` object.
     */
    YaProcessData(const Steinberg::Vst::ProcessData& process_data);

    /**
     * Reconstruct the original `ProcessData` object passed to the constructor
     * and return it. This is used in the Wine plugin host when processing an
     * `IAudioProcessor::process()` call.
     */
    Steinberg::Vst::ProcessData& get();

    /**
     * **Move** all output written by the Windows VST3 plugin to a response
     * object that can be used to write those results back to the host.
     */
    YaProcessDataResponse move_outputs_to_response();

    template <typename S>
    void serialize(S& s) {
        s.value4b(process_mode);
        s.value4b(symbolic_sample_size);
        s.value4b(num_samples);
        s.container(inputs, max_num_speakers);
        s.container4b(outputs_num_channels, max_num_speakers);
        s.container(input_parameter_changes, 1 << 16);
    }

   private:
    /**
     * The process data we reconstruct from the other fields during `get()`.
     */
    Steinberg::Vst::ProcessData reconstructed_process_data;

    /**
     * The processing mode copied directly from the input struct.
     */
    Steinberg::Vst::ProcessModes process_mode;

    /**
     * The symbolic sample size (see `Steinberg::Vst::SymbolicSampleSizes`) is
     * important. The audio buffers are represented by as a C-style untagged
     * union of array of either single or double precision floating point
     * arrays. This field determines which of those variants should be used.
     */
    Steinberg::Vst::SymbolicSampleSizes symbolic_sample_size;

    /**
     * The number of samples in each audio buffer.
     */
    int32 num_samples;

    /**
     * In `ProcessData` they use C-style heap arrays, so they have to store the
     * number of input/output busses, and then also store pointers to the first
     * audio buffer object. We can combine these two into vectors.
     */
    std::vector<YaAudioBusBuffers> inputs;

    /**
     * For the outputs we only have to keep track of how many output channels
     * each bus has. From this and from `num_samples` we can reconstruct the
     * output buffers on the Wine side of the process call.
     */
    std::vector<int32> outputs_num_channels;

    /**
     * Incoming parameter changes.
     */
    YaParameterChanges input_parameter_changes;

    // TODO: Add these (but since these require interface implementations we'll
    //       do it in a second round)
    /*
    IEventList* inputEvents;   ///< incoming events for this block (optional)
    IEventList* outputEvents;  ///< outgoing events for this block (optional)
    ProcessContext*
        processContext;  ///< processing context (optional, but most welcome)
    */
};
