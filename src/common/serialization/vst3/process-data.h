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

#pragma once

#include <variant>

#include <bitsery/ext/std_optional.h>
#include <bitsery/ext/std_variant.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>

#include "base.h"
#include "event-list.h"
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
     * We only provide a default constructor here, because we need to fill the
     * existing object with new audio data every processing cycle to avoid
     * reallocating a new object every time.
     */
    YaAudioBusBuffers() noexcept;

    /**
     * Create a new, zero initialize audio bus buffers object. Used to
     * reconstruct the output buffers during `YaProcessData::reconstruct()`.
     */
    void clear(int32 sample_size, size_t num_samples, size_t num_channels);

    /**
     * Copy data from a host provided `AudioBusBuffers` object during a process
     * call. Used in `YaProcessData::repopulate()`. Since `AudioBusBuffers`
     * contains an untagged union for storing single and double precision
     * floating point values, the original `ProcessData`'s `symbolicSampleSize`
     * field determines which variant of that union to use. Similarly the
     * `ProcessData`' `numSamples` field determines the extent of these arrays.
     */
    void repopulate(int32 sample_size,
                    int32 num_samples,
                    const Steinberg::Vst::AudioBusBuffers& data);

    /**
     * Reconstruct the original `AudioBusBuffers` object passed to the
     * constructor and return it. This is used as part of
     * `YaProcessData::reconstruct()`. The object contains pointers to
     * `buffers`, so it may not outlive this object.
     *
     * NOTE: The `silenceFlags` field is of course not a reference, so writing
     *       to that will not modify `silence_flags`.
     */
    void reconstruct(Steinberg::Vst::AudioBusBuffers& reconstructed_buffers);

    /**
     * Return the number of channels in `buffers`. Only used for debug logs.
     */
    size_t num_channels() const;

    /**
     * Write these buffers and the silence flag back to an `AudioBusBuffers
     * object provided by the host.
     */
    void write_back_outputs(
        Steinberg::Vst::AudioBusBuffers& output_buffers) const;

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

    /**
     * A bitfield for silent channels copied directly from the input struct.
     *
     * We could have done some optimizations to avoid unnecessary copying when
     * these silence flags are set, but since it's an optional feature we
     * shouldn't risk it.
     */
    uint64 silence_flags = 0;

   private:
    /**
     * We need these during the reconstruction process to provide a pointer to
     * an array of pointers to the actual buffers.
     */
    std::vector<void*> buffer_pointers;

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
 * A serializable wrapper around `ProcessData`. We'll read all information from
 * the host so we can serialize it and provide an equivalent `ProcessData`
 * struct to the plugin. Then we can create a `YaProcessData::Response` object
 * that contains all output values so we can write those back to the host.
 *
 * Be sure to double check how `YaProcessData::Response` is used. We do some
 * pointer tricks there to avoid copies and moves when serializing the results
 * of our audio processing.
 */
class YaProcessData {
   public:
    /**
     * Initialize the process data. We only provide a default constructor here,
     * because we need to fill the existing object with new data every
     * processing cycle to avoid reallocating a new object every time.
     */
    YaProcessData() noexcept;

    /**
     * Copy data from a host provided `ProcessData` object during a process
     * call. This struct can then be serialized, and
     * `YaProcessData::reconstruct()` can then be used again to recreate the
     * original `ProcessData` object. This will avoid allocating unless it's
     * absolutely necessary (e.g. when we receive more parameter changes than
     * we've received in previous calls).
     */
    void repopulate(const Steinberg::Vst::ProcessData& process_data);

    /**
     * Reconstruct the original `ProcessData` object passed to the constructor
     * and return it. This is used in the Wine plugin host when processing an
     * `IAudioProcessor::process()` call.
     */
    Steinberg::Vst::ProcessData& reconstruct();

    /**
     * A serializable wrapper around the output fields of `ProcessData`, so we
     * only have to copy the information back that's actually important. These
     * fields are pointers to the corresponding fields in `YaProcessData`. On
     * the plugin side this information can then be written back to the host.
     *
     * HACK: All of this is an optimization to avoid unnecessarily copying or
     *       moving and reallocating. Directly serializing and deserializing
     *       from and to pointers does make all of this very error prone, hence
     *       all of the assertions.
     *
     * @see YaProcessData
     */
    struct Response {
        // We store raw pointers instead of references so we can default
        // initialize this object during deserialization
        std::vector<YaAudioBusBuffers>* outputs = nullptr;
        std::optional<YaParameterChanges>* output_parameter_changes = nullptr;
        std::optional<YaEventList>* output_events = nullptr;

        template <typename S>
        void serialize(S& s) {
            assert(outputs && output_parameter_changes && output_events);
            // Since these fields are references to the corresponding fields on
            // the surrounding object, we're actually serializing those fields.
            // This means that on the plugin side we can _only_ deserialize into
            // an existing object, since our serializing code doesn't touch the
            // actual pointers.
            s.container(*outputs, max_num_speakers);
            s.ext(*output_parameter_changes, bitsery::ext::StdOptional{});
            s.ext(*output_events, bitsery::ext::StdOptional{});
        }
    };

    /**
     * Create a `YaProcessData::Response` object that refers to the output
     * fields in this object. The object doesn't store any actual data, and may
     * not outlive this object. We use this so we only have to copy the relevant
     * fields back to the host. On the Wine side this function should only be
     * called after we call the plugin's `IAudioProcessor::process()` function
     * with the reconstructed process data obtained from
     * `YaProcessData::reconstruct()`.
     *
     * On the plugin side this should be used to create a response object that
     * **must** be received into, since we're deserializing directly into some
     * pointers.
     */
    Response& create_response() noexcept;

    /**
     * Write all of this output data back to the host's `ProcessData` object.
     */
    void write_back_outputs(Steinberg::Vst::ProcessData& process_data);

    template <typename S>
    void serialize(S& s) {
        s.value4b(process_mode);
        s.value4b(symbolic_sample_size);
        s.value4b(num_samples);
        s.container(inputs, max_num_speakers);
        s.container4b(outputs_num_channels, max_num_speakers);
        s.object(input_parameter_changes);
        s.value1b(output_parameter_changes_supported);
        s.ext(input_events, bitsery::ext::StdOptional{});
        s.value1b(output_events_supported);
        s.ext(process_context, bitsery::ext::StdOptional{});

        // We of course won't serialize the `reconstructed_process_data` and all
        // of the `output*` fields defined below it
    }

    // These fields are input and context data read from the original
    // `ProcessData` object

    /**
     * The processing mode copied directly from the input struct.
     */
    int32 process_mode;

    /**
     * The symbolic sample size (see `Steinberg::Vst::SymbolicSampleSizes`) is
     * important. The audio buffers are represented by as a C-style untagged
     * union of array of either single or double precision floating point
     * arrays. This field determines which of those variants should be used.
     */
    int32 symbolic_sample_size;

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

    /**
     * Whether the host supports output parameter changes (depending on whether
     * `outputParameterChanges` was a null pointer or not).
     */
    bool output_parameter_changes_supported;

    /**
     * Incoming events.
     */
    std::optional<YaEventList> input_events;

    /**
     * Whether the host supports output events (depending on whether
     * `outputEvents` was a null pointer or not).
     */
    bool output_events_supported;

    /**
     * Some more information about the project and transport.
     */
    std::optional<Steinberg::Vst::ProcessContext> process_context;

   private:
    // These are the same fields as in `YaProcessData::Response`. We'll generate
    // these as part of creating `reconstructed_process_data`, and they will be
    // referred to in the response object created in `create_response()`

    /**
     * The outputs. Will be created based on `outputs_num_channels` (which
     * determines how many output busses there are and how many channels each
     * bus has) and `num_samples`.
     */
    std::vector<YaAudioBusBuffers> outputs;

    /**
     * The output parameter changes. Will be initialized depending on
     * `output_parameter_changes_supported`.
     */
    std::optional<YaParameterChanges> output_parameter_changes;

    /**
     * The output events. Will be initialized depending on
     * `output_events_supported`.
     */
    std::optional<YaEventList> output_events;

    // These last few members are used on the Wine plugin host side to
    // reconstruct the original `ProcessData` object. Here we also initialize
    // these `output*` fields so the Windows VST3 plugin can write to them
    // though a regular `ProcessData` object. Finally we can wrap these output
    // fields back into a `YaProcessData::Response` using
    // `create_response()`. so they can be serialized and written back
    // to the host's `ProcessData` object.

    /**
     * This is a `Response` object that refers to the fields below.
     *
     * NOTE: We use this on the plugin side as an optimization to be able to
     *       directly receive data into this object, avoiding the need for any
     *       allocations.
     */
    Response response_object;

    /**
     * Obtained by calling `.get()` on every `YaAudioBusBuffers` object in
     * `intputs`. These objects contain pointers to the data in `inputs` and may
     * thus not outlive them.
     */
    std::vector<Steinberg::Vst::AudioBusBuffers> inputs_audio_bus_buffers;

    /**
     * Obtained by calling `.get()` on every `YaAudioBusBuffers` object in
     * `outputs`. These objects contain pointers to the data in `outputs` and
     * may thus not outlive them. These are created in a two step process, since
     * we first have to create `outputs` from `outputs_num_channels` before we
     * can transform it into a structure the Windows VST3 plugin can work with.
     * Hooray for heap arrays.
     */
    std::vector<Steinberg::Vst::AudioBusBuffers> outputs_audio_bus_buffers;

    /**
     * The process data we reconstruct from the other fields during `get()`.
     */
    Steinberg::Vst::ProcessData reconstructed_process_data;
};

namespace Steinberg {
namespace Vst {
template <typename S>
void serialize(S& s, Steinberg::Vst::ProcessContext& process_context) {
    // The docs don't mention that things ever got added to this context (and
    // that some fields thus may not exist for all hosts), so we'll just
    // directly serialize everything. If it does end up being the case that new
    // fields were added here we should serialize based on the bits set in the
    // flags bitfield.
    s.value4b(process_context.state);
    s.value8b(process_context.sampleRate);
    s.value8b(process_context.projectTimeSamples);
    s.value8b(process_context.systemTime);
    s.value8b(process_context.continousTimeSamples);
    s.value8b(process_context.projectTimeMusic);
    s.value8b(process_context.barPositionMusic);
    s.value8b(process_context.cycleStartMusic);
    s.value8b(process_context.cycleEndMusic);
    s.value8b(process_context.tempo);
    s.value4b(process_context.timeSigNumerator);
    s.value4b(process_context.timeSigDenominator);
    s.object(process_context.chord);
    s.value4b(process_context.smpteOffsetSubframes);
    s.value4b(process_context.smpteOffsetSubframes);
    s.object(process_context.frameRate);
    s.value4b(process_context.samplesToNextClock);
}

template <typename S>
void serialize(S& s, Steinberg::Vst::Chord& chord) {
    s.value1b(chord.keyNote);
    s.value1b(chord.rootNote);
    s.value2b(chord.chordMask);
}

template <typename S>
void serialize(S& s, Steinberg::Vst::FrameRate& frame_rate) {
    s.value4b(frame_rate.framesPerSecond);
    s.value4b(frame_rate.flags);
}
}  // namespace Vst
}  // namespace Steinberg
