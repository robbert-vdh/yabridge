// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2024 Robbert van der Helm
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

#pragma once

#include <optional>

#include <clap/process.h>
#include <llvm/small-vector.h>

#include "../../audio-shm.h"
#include "../../bitsery/ext/in-place-optional.h"
#include "audio-buffer.h"
#include "events.h"

// Serialization messages for `clap/process.h`

namespace clap {
namespace process {

/**
 * A serializable wrapper around `clap_process_t`. This works exactly the same
 * as the process data wrapper for VST3. At the start of a process cycle all
 * audio data is copied to the already set up shared memory buffers and the
 * event and transport data is copied to this object. Then on the Wine side
 * we'll reconstruct the original `clap_process_t` and pass that to the plugin.
 * The output events are then sent back to the native plugin, where it can write
 * those along with the audio outputs (which are also in that shared memory
 * buffer) back to the host. The response is serialized in the
 * `clap::process::Process::Response` object since only a small amount of
 * information needs to be sent back.
 *
 * As mentioned earlier, the audio data is not stored in this object but it is
 * instead stored in a shared memory object shared by a `clap_plugin_proxy` and
 * `ClapPluginInstance` pair. Since the amount of audio data sent per processing
 * call is fixed, this halves the number of required memory copies.
 *
 * Be sure to double check how `clap::process::Process::Response` is used. We do
 * some pointer tricks there to avoid copies and moves when serializing the
 * results of our audio processing.
 */
class Process {
   public:
    /**
     * Initialize the process data. We only provide a default constructor here,
     * because we need to fill the existing object with new data every
     * processing cycle to avoid reallocating a new object every time.
     */
    Process() noexcept;

    /**
     * Copy data from a host provided `clap_process_T` object to this struct
     * during a process call. This struct can then be serialized, and
     * `reconstruct()` can then be used again to recreate the original
     * `clap_process_t` object. This avoids allocating unless it's absolutely
     * necessary (e.g. when we receive more events than we've received in
     * previous calls).
     *
     * The input audio buffer will be copied to `shared_audio_buffers`. There's
     * no direct link between this `Process` object and those buffers, but they
     * should be treated as a pair. This is a bit ugly, but optimizations sadly
     * never made code prettier.
     */
    void repopulate(const clap_process_t& process,
                    AudioShmBuffer& shared_audio_buffers);

    /**
     * Reconstruct the original `clap_process_t` object passed to `repopulate()`
     * and return it. This is used in the Wine plugin host when handling a
     * `clap_plugin::process()` call.
     *
     * Because the actual audio is stored in an `AudioShmBuffer` outside of this
     * object, we need to make sure that the `AudioBusBuffers` objects we're
     * using point to the correct buffer even after a resize. To make it more
     * difficult for us to mess this up, we'll store those bus-channel pointers
     * in `ClapBridge::ClapPluginInstance` and we'll point the pointers in our
     * `inputs` and `outputs` fields directly to those pointers. They will have
     * been set up during `clap_plugin::activate()`.
     *
     * CLAP allows mixed float and double precision audio if the plugin opts
     * into it. The audio buffers thus always contain enough space for double
     * precision if a port supports it. The actual sample format used is stored
     * in our `clap::audio_buffer::AudioBuffer` serialization wrapper.
     */
    const clap_process_t& reconstruct(
        std::vector<std::vector<void*>>& input_pointers,
        std::vector<std::vector<void*>>& output_pointers);

    /**
     * A serializable wrapper around the output fields of `clap_process_t`, so
     * we only have to copy the information back that's actually important.
     * These fields are pointers to the corresponding fields in `Process`. On
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
        llvm::SmallVectorImpl<clap_audio_buffer_t>* audio_outputs = nullptr;
        clap::events::EventList* out_events = nullptr;

        template <typename S>
        void serialize(S& s) {
            assert(audio_outputs && out_events);
            // Since these fields are references to the corresponding fields on
            // the surrounding object, we're actually serializing those fields.
            // This means that on the plugin side we can _only_ deserialize into
            // an existing object, since our serializing code doesn't touch the
            // actual pointers.
            s.container(*audio_outputs, 1 << 14);
            s.object(*out_events);
        }
    };

    /**
     * Create a `clap::process::Process::Response` object that refers to the
     * output fields in this object. The object doesn't store any actual data,
     * and may not outlive this object. We use this so we only have to copy the
     * relevant fields back to the host. On the Wine side this function should
     * only be called after the plugin's `clap_plugin::process()` function has
     * been called with the reconstructed process data obtained from
     * `Process::reconstruct()`.
     *
     * On the plugin side this should be used to create a response object that
     * **must** be received into, since we're deserializing directly into some
     * pointers.
     */
    Response& create_response() noexcept;

    /**
     * Write all of this output data back to the host's `clap_process_t` object.
     * During this process we'll also write the output audio from the
     * corresponding shared memory audio buffers back.
     */
    void write_back_outputs(const clap_process_t& process,
                            const AudioShmBuffer& shared_audio_buffers);

    template <typename S>
    void serialize(S& s) {
        s.value8b(steady_time_);
        s.value4b(frames_count_);

        s.ext(transport_, bitsery::ext::InPlaceOptional{});

        // Both `audio_inputs_` and `audio_outputs_` only store metadata. The
        // actual audio is sent using an accompanying `AudioShmBuffer` object.
        s.container(audio_inputs_, 1 << 14);
        s.container1b(audio_inputs_type_, 1 << 14);
        s.container(audio_outputs_, 1 << 14);
        s.container1b(audio_outputs_type_, 1 << 14);

        // We don't need to serialize the output events because this will always
        // be empty on the Wine side. The response is sent back through the
        // separate `Response` object
        s.object(in_events_);
    }

    // These fields are input and context data read from the original
    // `clap_process_t` object

    int64_t steady_time_ = 0;
    uint32_t frames_count_ = 0;

    // This is an optional field
    std::optional<clap_event_transport_t> transport_;

    /**
     * The audio input buffers for every port. We'll only serialize the metadata
     * During `reconstruct()` the channel pointers pointers in these objects
     * will be set to point to our shared memory surface that holds the actual
     * audio data.
     */
    llvm::SmallVector<clap_audio_buffer_t, 8> audio_inputs_;
    /**
     * The types corresponding to each buffer in `audio_inputs_`. This needs to
     * be serialized separately since this information is encoded by setting one
     * of the two pointers instead of through a flag.
     */
    llvm::SmallVector<clap::audio_buffer::AudioBufferType, 8>
        audio_inputs_type_;

    /**
     * The audio output buffers for every port. We'll only serialize the
     * metadata During `reconstruct()` the channel pointers pointers in these
     * objects will be set to point to our shared memory surface that holds the
     * actual audio data.
     */
    llvm::SmallVector<clap_audio_buffer_t, 8> audio_outputs_;
    /**
     * The types corresponding to each buffer in `audio_outputs_`. This needs to
     * be serialized separately since this information is encoded by setting one
     * of the two pointers instead of through a flag.
     */
    llvm::SmallVector<clap::audio_buffer::AudioBufferType, 8>
        audio_outputs_type_;

    clap::events::EventList in_events_;
    clap::events::EventList out_events_;

   private:
    // These last few members are used on the Wine plugin host side to
    // reconstruct the original `clap_process_t` object. Here we also initialize
    // these output fields so the Windows CLAP plugin can write to them though a
    // regular `ProcessData` object. Finally we can wrap these output fields
    // back into a `clap::process::Process::Response` using `create_response()`.
    // so they can be serialized and written back to the host's `clap_process_t`
    // object.

    /**
     * This is a `Response` object that contains pointers to other fields in
     * this struct so we can serialize to and from them.
     *
     * NOTE: We use this on the plugin side as an optimization to be able to
     *       directly receive data into this object, avoiding the need for any
     *       allocations.
     */
    Response response_object_;

    /**
     * The process data we reconstruct from the other fields during
     * `reconstruct()`.
     */
    clap_process_t reconstructed_process_data_{};
};

}  // namespace process
}  // namespace clap
