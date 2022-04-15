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

#include <variant>

#include <pluginterfaces/vst/ivstaudioprocessor.h>

#include "../../audio-shm.h"
#include "../../bitsery/ext/in-place-optional.h"
#include "../../bitsery/ext/in-place-variant.h"
#include "base.h"
#include "event-list.h"
#include "parameter-changes.h"

// This header provides serialization wrappers around `ProcessData`

/**
 * A serializable wrapper around `ProcessData`. We'll read all information from
 * the host so we can serialize it and provide an equivalent `ProcessData`
 * struct to the Windows VST3 plugin. Then we can create a
 * `YaProcessData::Response` object that contains all output values so we can
 * write those back to the host.
 *
 * As an optimization, this no longer stores any actual audio. Instead, both
 * `Vst3PluginProxyImpl` and `Vst3Bridge::InstanceInterfaces` contain a shared
 * memory object that stores the audio buffers used for the plugin instance.
 * This object is then sent alongside it with auxiliary information. This
 * prevents a lot of unnecessary copies.
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
     *
     * During this process the input audio will be written to
     * `shared_audio_buffers`. There's no direct link between this
     * `YaProcessData` object and those buffers, but they should be used as a
     * pair. This is a bit ugly, but optimizations sadly never made code
     * prettier.
     */
    void repopulate(const Steinberg::Vst::ProcessData& process_data,
                    AudioShmBuffer& shared_audio_buffers);

    /**
     * Reconstruct the original `ProcessData` object passed to the constructor
     * and return it. This is used in the Wine plugin host when processing an
     * `IAudioProcessor::process()` call.
     *
     * Because the actual audio is stored in an `AudioShmBuffer` outside of this
     * object, we need to make sure that the `AudioBusBuffers` objects we're
     * using point to the correct buffer even after a resize. To make it more
     * difficult for us to mess this up, we'll store those bus-channel pointers
     * in `Vst3Bridge::InstanceInterfaces` and we'll point the pointers in our
     * `inputs` and `outputs` fields directly to those pointers. They will have
     * been set up during `IAudioProcessor::setupProcessing()`.
     *
     * These can be either float or double pointers. Since a pointer is a
     * pointer and they're stored using a union the actual type doesn't matter,
     * but we'll accept these as void pointers since the stride will be
     * different depending on whether the host is going to be sending double or
     * single precision audio.
     */
    Steinberg::Vst::ProcessData& reconstruct(
        std::vector<std::vector<void*>>& input_pointers,
        std::vector<std::vector<void*>>& output_pointers);

    /**
     * A serializable wrapper around the output fields of `ProcessData`, so we
     * only have to copy the information back that's actually important. These
     * fields are pointers to the corresponding fields in `YaProcessData`. On
     * the plugin side this information can then be written back to the host.
     * The actual output audio is stored in the shared memory object.
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
        llvm::SmallVectorImpl<Steinberg::Vst::AudioBusBuffers>* outputs =
            nullptr;
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
            s.ext(*output_parameter_changes, bitsery::ext::InPlaceOptional{});
            s.ext(*output_events, bitsery::ext::InPlaceOptional{});
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
     * During this process we'll also write the output audio from the
     * corresponding shared memory audio buffers back.
     */
    void write_back_outputs(Steinberg::Vst::ProcessData& process_data,
                            const AudioShmBuffer& shared_audio_buffers);

    template <typename S>
    void serialize(S& s) {
        s.value4b(process_mode_);
        s.value4b(symbolic_sample_size_);
        s.value4b(num_samples_);

        // Both of these fields only store metadata. The actual audio is sent
        // using an accompanying `AudioShmBuffer` object.
        s.container(inputs_, max_num_speakers);
        s.container(outputs_, max_num_speakers);

        // The output parameter changes and events will remain empty on the
        // plugin side, so by serializing them we merely indicate to the Wine
        // plugin host whether the host supports them or not
        s.object(input_parameter_changes_);
        s.ext(output_parameter_changes_, bitsery::ext::InPlaceOptional{});
        s.ext(input_events_, bitsery::ext::InPlaceOptional{});
        s.ext(output_events_, bitsery::ext::InPlaceOptional{});

        s.ext(process_context_, bitsery::ext::InPlaceOptional{});

        // We of course won't serialize the `reconstructed_process_data` and all
        // of the `output*` fields defined below it
    }

    // These fields are input and context data read from the original
    // `ProcessData` object

    /**
     * The processing mode copied directly from the input struct.
     */
    int32 process_mode_;

    /**
     * The symbolic sample size (see `Steinberg::Vst::SymbolicSampleSizes`) is
     * important. The audio buffers are represented by as a C-style untagged
     * union of array of either single or double precision floating point
     * arrays. This field determines which of those variants should be used.
     */
    int32 symbolic_sample_size_;

    /**
     * The number of samples in each audio buffer.
     */
    int32 num_samples_;

    /**
     * This contains metadata about the input buffers for every bus. During
     * `reconstruct()` the channel pointers contained within these objects will
     * be set to point to our shared memory surface that holds the actual audio
     * data.
     */
    llvm::SmallVector<Steinberg::Vst::AudioBusBuffers, 8> inputs_;

    /**
     * This contains metadata about the output buffers for every bus. During
     * `reconstruct()` the channel pointers contained within these objects will
     * be set to point to our shared memory surface that holds the actual audio
     * data.
     */
    llvm::SmallVector<Steinberg::Vst::AudioBusBuffers, 8> outputs_;

    /**
     * Incoming parameter changes.
     */
    YaParameterChanges input_parameter_changes_;

    /**
     * If the host supports it, this will allow the plugin to output parameter
     * changes. Otherwise we'll also pass a null pointer to the plugin.
     */
    std::optional<YaParameterChanges> output_parameter_changes_;

    /**
     * Incoming events.
     */
    std::optional<YaEventList> input_events_;

    /**
     * If the host supports it, this will allow the plugin to output events,
     * such as note events. Otherwise we'll also pass a null pointer to the
     * plugin.
     */
    std::optional<YaEventList> output_events_;

    /**
     * Some more information about the project and transport.
     */
    std::optional<Steinberg::Vst::ProcessContext> process_context_;

   private:
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
    Response response_object_;

    /**
     * The process data we reconstruct from the other fields during `get()`.
     */
    Steinberg::Vst::ProcessData reconstructed_process_data_;
};

namespace Steinberg {
namespace Vst {
template <typename S>
void serialize(S& s, Steinberg::Vst::AudioBusBuffers& buffers) {
    // We don't don't touch the audio pointers. Those should point to the
    // correct positions in the corresponding `AudioShmBuffer` object.
    s.value4b(buffers.numChannels);
    s.value8b(buffers.silenceFlags);
}

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
