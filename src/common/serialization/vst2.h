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

#include <bitsery/traits/array.h>
#include <bitsery/traits/vector.h>
#include <llvm/small-vector.h>
#include <vestige/aeffectx.h>

#include "../audio-shm.h"
#include "../bitsery/ext/in-place-optional.h"
#include "../bitsery/ext/in-place-variant.h"
#include "../bitsery/traits/small-vector.h"
#include "../utils.h"
#include "../vst24.h"
#include "common.h"

// These constants are limits used by bitsery

/**
 * The maximum number of audio channels supported. Some plugins report a huge
 * amount of input channels, even though they don't even process any incoming
 * audio. Renoise seems to report 112 speakers per audio channel, so this limit
 * is now quite a bit higher than it should have to be.
 */
constexpr size_t max_audio_channels = 16384;
/**
 * The maximum number of samples in a buffer.
 */
constexpr size_t max_buffer_size = 1 << 16;
/**
 * The maximum number of MIDI events in a single `VstEvents` struct. Apparently
 * the Orchestral Tools Kontakt libraries output more than 2048 MIDI events per
 * buffer, which sounds kinda intense, so hopefully this is enough.
 */
constexpr size_t max_midi_events = max_buffer_size;
/**
 * The maximum size in bytes of a string or buffer passed through a void pointer
 * in one of the dispatch functions. This is used to create buffers for plugins
 * to write strings to.
 */
[[maybe_unused]] constexpr size_t max_string_length = 64;

/**
 * The maximum size for the buffer we're receiving chunks in. Allows for up to
 * 50 MB chunks. Hopefully no plugin will come anywhere near this limit, but it
 * will add up when plugins start to audio include samples in their presets.
 */
constexpr size_t binary_buffer_size = 50 << 20;

/**
 * Update an `AEffect` object, copying values from `updated_plugin` to `plugin`.
 * This will copy all flags and regular values, leaving all pointers in `plugin`
 * untouched. This should be updating the same values as the serialization
 * function right below this.
 */
AEffect& update_aeffect(AEffect& plugin,
                        const AEffect& updated_plugin) noexcept;

/**
 * Wrapper for chunk data.
 */
struct ChunkData {
    using Response = std::nullptr_t;

    std::vector<uint8_t> buffer;

    template <typename S>
    void serialize(S& s) {
        s.container1b(buffer, binary_buffer_size);
    }
};

/**
 * A wrapper around `VstEvents` that stores the data in a vector instead of a
 * C-style array. Needed until bitsery supports C-style arrays
 * https://github.com/fraillt/bitsery/issues/28. An advantage of this approach
 * is that RAII will handle cleanup for us. We'll handle both regular MIDI
 * events as well as SysEx here. If we somehow encounter a different kind of
 * event, we'll just treat it as regular MIDI and print a warning.
 *
 * Before serialization the events are read from a C-style array into a vector
 * using this class's constructor, and after deserializing the original struct
 * can be reconstructed using the `as_c_events()` method.
 *
 * Using preallocated small vectors here gets rid of all event related
 * allocations in normal use cases.
 */
class alignas(16) DynamicVstEvents {
   public:
    using Response = std::nullptr_t;

    DynamicVstEvents() noexcept;

    explicit DynamicVstEvents(const VstEvents& c_events);

    /**
     * Construct a `VstEvents` struct from the events vector. This contains a
     * pointer to that vector's elements, so the returned object should not
     * outlive this struct.
     */
    VstEvents& as_c_events();

    /**
     * MIDI events are sent just before the audio processing call. Technically a
     * host can call `effProcessEvents()` multiple times, but in practice this
     * of course doesn't happen. In case the host or plugin sent SysEx data, we
     * will need to update the `dumpBytes` field to point to the data stored in
     * the `sysex_data_` field before dumping everything to
     * `vst_events_buffer_`.
     */
    llvm::SmallVector<VstEvent, 64> events_;

    /**
     * If the host or a plugin sends SysEx data, then we will store that data
     * here. I've only seen this happen with the combination of an Arturia
     * MiniLab keyboard, REAPER, and D16 Group plugins. We'll store this as an
     * associative list of `(index, data)` pairs, where `index` corresponds to
     * an event in `events`. There's no 'SmallUnorderedMap' equivalent to the
     * `SmallVector`.
     */
    llvm::SmallVector<std::pair<native_size_t, std::string>, 8> sysex_data_;

    template <typename S>
    void serialize(S& s) {
        s.container(events_, max_midi_events,
                    [](S& s, VstEvent& event) { s.container1b(event.dump); });
        s.container(sysex_data_, max_midi_events,
                    [](S& s, std::pair<native_size_t, std::string>& pair) {
                        s.value8b(pair.first);
                        s.text1b(pair.second, max_buffer_size);
                    });
    }

   private:
    /**
     * Some buffer we can build a `VstEvents` object in. This object can be
     * populated with contents of the `VstEvents` vector using the
     * `as_c_events()` method.
     *
     * The reason why this is necessary is because the `VstEvents` struct is
     * actuall variable size object. In the definition in
     * `vestige/aeffectx.h` the struct contains a single element `VstEvent`
     * pointer array, but the actual length of this array is
     * `VstEvents::numEvents`. Because there is no real limit on the number of
     * MIDI events the host can send at once we have to build this object on the
     * heap by hand.
     */
    llvm::SmallVector<
        uint8_t,
        sizeof(VstEvents) +
            ((64 - 1) *
             sizeof(VstEvent*))>  // NOLINT(bugprone-sizeof-expression)
        vst_events_buffer_;
};

/**
 * A wrapper around `VstSpeakerArrangement` that works the same way as the above
 * wrapper for `VstEvents`. This is needed because the `VstSpeakerArrangement`
 * struct is actually a variable sized array. Even though it will be very
 * unlikely that we'll encounter systems with more than 8 speakers, it is
 * something we should be able to support.
 *
 * Before serialization the events are read from a C-style array into a vector
 * using this class's constructor, and after deserializing the original struct
 * can be reconstructed using the `as_c_speaker_arrangement()` method.
 */
class alignas(16) DynamicSpeakerArrangement {
   public:
    using Response = DynamicSpeakerArrangement;

    DynamicSpeakerArrangement() noexcept;

    explicit DynamicSpeakerArrangement(
        const VstSpeakerArrangement& speaker_arrangement);

    /**
     * Construct a dynamically sized `VstSpeakerArrangement` object based on
     * this object.
     */
    VstSpeakerArrangement& as_c_speaker_arrangement();

    /**
     * Reconstruct the dynamically sized `VstSpeakerArrangement` object and
     * return the raw data buffer. Needed to write the results back to the host
     * since we can't just reassign the object.
     */
    std::vector<uint8_t>& as_raw_data();

    /**
     * The flags field from `VstSpeakerArrangement`
     */
    int flags_;

    /**
     * Information about the speakers in a particular input or output
     * configuration.
     */
    std::vector<VstSpeaker> speakers_;

    template <typename S>
    void serialize(S& s) {
        s.value4b(flags_);
        s.container(
            speakers_, max_audio_channels,
            [](S& s, VstSpeaker& speaker) { s.container1b(speaker.data); });
    }

   private:
    /**
     * Some buffer we can build a `VstSpeakerArrangement` object in. This object
     * can be populated using the `as_c_speaker_arrangement()` method.
     *
     * This is necessary because the `VstSpeakerArrangement` struct contains a
     * dynamically sized array of length `VstSpeakerArrangement::num_speakers`.
     * We build this object in a byte sized vector to make allocating enough
     * heap space easy and safe.
     */
    std::vector<uint8_t> speaker_arrangement_buffer_;
};

/**
 * Marker struct to indicate that the other side (the Wine plugin host) should
 * send an updated copy of the plugin's `AEffect` object. Should not be needed
 * since the plugin should be calling `audioMasterIOChanged()` after it has
 * changed its object, but some improperly coded plugins will only initialize
 * their flags, IO properties and parameter counts after `effEditOpen()`.
 */
struct WantsAEffectUpdate {
    using Response = AEffect;

    template <typename S>
    void serialize(S&) {}
};

/**
 * Marker struct to indicate that the Wine plugin host should set up shared
 * memory buffers for audio processing. The size for this depends on the maximum
 * block size indicated by the host using `effSetBlockSize()` and whether the
 * host called `effSetProcessPrecision()` to indicate that the plugin is going
 * to receive double precision audio or not.
 *
 * HACK: We need to do some manual work after the plugin has handled
 *       `effMainsChanged`, and our current setup doesn't allow us to do that
 *       from the `passthrough_event()` function. So for the time being we'll
 *       have to do this manually in the `receive_events()` handler, see
 *       `Vst2Bridge::run()`.
 */
struct WantsAudioShmBufferConfig {
    using Response = AudioShmBuffer::Config;

    template <typename S>
    void serialize(S&) {}
};

/**
 * Marker struct to indicate that that the event writes arbitrary data into one
 * of its own buffers and uses the void pointer to store start of that data,
 * with the return value indicating the size of the array.
 */
struct WantsChunkBuffer {
    using Response = ChunkData;

    template <typename S>
    void serialize(S&) {}
};

/**
 * Marker struct to indicate that the event handler will write a pointer to a
 * `VstRect` struct into the void pointer. It's also possible that the plugin
 * doesn't do anything. In that case we'll serialize the response as a null
 * pointer.
 */
struct WantsVstRect {
    using Response = VstRect;

    template <typename S>
    void serialize(S&) {}
};

/**
 * Marker struct to indicate that the event handler will return a pointer to a
 * `VstTimeInfo` struct that should be returned transfered.
 */
struct WantsVstTimeInfo {
    using Response = VstTimeInfo;

    template <typename S>
    void serialize(S&) {}
};

/**
 * Marker struct to indicate that that the event requires some buffer to write
 * a C-string into.
 */
struct WantsString {
    using Response = std::string;

    template <typename S>
    void serialize(S&) {}
};

/**
 * AN instance of this should be sent back as a response to an incoming event.
 */
struct Vst2EventResult {
    /**
     * The response for an event. This is usually either:
     *
     * - Nothing, on which case only the return value from the callback function
     *   gets passed along.
     * - A (short) string.
     * - Some binary blob stored as a byte vector. During `effGetChunk` this
     *   will contain some chunk data that should be written to
     *   `Vst2PluginBridge::chunk_data`.
     * - A specific struct in response to an event such as `audioMasterGetTime`
     *   or `audioMasterIOChanged`.
     * - An X11 window pointer for the editor window.
     *
     * @relates passthrough_event
     */
    using Payload = std::variant<std::nullptr_t,
                                 std::string,
                                 AEffect,
                                 AudioShmBuffer::Config,
                                 ChunkData,
                                 DynamicSpeakerArrangement,
                                 VstIOProperties,
                                 VstMidiKeyName,
                                 VstParameterProperties,
                                 VstRect,
                                 VstTimeInfo>;

    /**
     * The result that should be returned from the dispatch function.
     */
    native_intptr_t return_value;
    /**
     * Events typically either just return their return value or write a string
     * into the void pointer, but sometimes an event response should forward
     * some kind of special struct.
     */
    Payload payload;
    /**
     * The same as the above value, but for returning values written to the
     * `intptr_t` value parameter. This is only used during
     * `effGetSpeakerArrangement`.
     */
    std::optional<Payload> value_payload;

    template <typename S>
    void serialize(S& s) {
        s.value8b(return_value);

        s.object(payload);
        s.ext(value_payload, bitsery::ext::InPlaceOptional());
    }
};

template <typename S>
void serialize(S& s, Vst2EventResult::Payload& payload) {
    s.ext(payload,
          bitsery::ext::InPlaceVariant{[](S&, std::nullptr_t&) {},
                                       [](S& s, std::string& string) {
                                           s.text1b(string, max_string_length);
                                       },
                                       [](S& s, auto& o) { s.object(o); }});
}

/**
 * An event as dispatched by the VST host. These events will get forwarded to
 * the VST host process running under Wine. The fields here mirror those
 * arguments sent to the `AEffect::dispatch` function.
 */
struct Vst2Event {
    using Result = Vst2EventResult;

    /**
     * VST events are passed a void pointer that can contain a variety of
     * different data types depending on the event's opcode. This is typically
     * either:
     *
     * - A null pointer, used for simple events.
     * - A char pointer to a null terminated string, used for passing strings to
     *   the plugin such as when renaming presets. Bitsery handles the
     *   serialization for us.
     *
     *   NOTE: Bitsery does not support null terminated C-strings without a
     *         known size. We can replace `std::string` with `char*` once it
     *         does for clarity's sake.
     *
     * - A byte vector for handling chunk data during `effSetChunk()`. We can't
     *   reuse the regular string handling here since the data may contain null
     *   bytes and `std::string::as_c_str()` might cut off everything after the
     *   first null byte.
     * - An X11 window handle.
     * - Specific data structures from `aeffextx.h`. For instance an event with
     *   the opcode `effProcessEvents` the hosts passes a `VstEvents` struct
     *   containing MIDI events, and `audioMasterIOChanged` lets the host know
     *   that the `AEffect` struct has changed.
     *
     * - Some empty buffer for the plugin to write its own data to, for instance
     *   for a plugin to report its name or the label for a certain parameter.
     *   There are two separate cases here. This is typically a short null
     *   terminated C-string. We'll assume this as the default case when none of
     *   the above options apply.
     *
     *   - Either the plugin writes arbitrary data and uses its return value to
     *     indicate how much data was written (i.e. for the `effGetChunk`
     *     opcode). For this we use a vector of bytes instead of a string since
     *   - Or the plugin will write a short null terminated C-string there.
     *     We'll assume that this is the default if none of the above options
     *     apply.
     *
     * @relates passthrough_event
     */
    using Payload = std::variant<std::nullptr_t,
                                 std::string,
                                 native_size_t,
                                 AEffect,
                                 ChunkData,
                                 DynamicVstEvents,
                                 DynamicSpeakerArrangement,
                                 WantsAEffectUpdate,
                                 WantsAudioShmBufferConfig,
                                 WantsChunkBuffer,
                                 VstIOProperties,
                                 VstMidiKeyName,
                                 VstParameterProperties,
                                 VstPatchChunkInfo,
                                 WantsVstRect,
                                 WantsVstTimeInfo,
                                 WantsString>;

    int opcode;
    int index;
    native_intptr_t value;
    float option;
    /**
     * The event dispatch function has a void pointer parameter that's often
     * used to either pass additional data for the event or to provide a buffer
     * for the plugin to write a string into.
     *
     * The `VstEvents` struct passed for the `effProcessEvents` event contains
     * an array of pointers. This requires some special handling which is why we
     * have to use an `std::variant` instead of a simple string buffer. Luckily
     * Bitsery can do all the hard work for us.
     */
    Payload payload;
    /**
     * The same as the above value, but for values passed through the `intptr_t`
     * value parameter. `effGetSpeakerArrangement` and
     * `effSetSpeakerArrangement` are the only events that use this.
     */
    std::optional<Payload> value_payload;

    template <typename S>
    void serialize(S& s) {
        s.value4b(opcode);
        s.value4b(index);
        s.value8b(value);
        s.value4b(option);

        s.object(payload);
        s.ext(value_payload, bitsery::ext::InPlaceOptional());
    }
};

template <typename S>
void serialize(S& s, Vst2Event::Payload& payload) {
    s.ext(payload,
          bitsery::ext::InPlaceVariant{[](S&, std::nullptr_t&) {},
                                       [](S& s, std::string& string) {
                                           s.text1b(string, max_string_length);
                                       },
                                       [](S& s, native_size_t& window_handle) {
                                           s.value8b(window_handle);
                                       },
                                       [](S& s, auto& o) { s.object(o); }});
}

/**
 * The result of a `getParameter` or a `setParameter` call. For `setParameter`
 * this struct won't contain any values and mostly acts as an acknowledgement
 * from the Wine plugin host.
 */
struct ParameterResult {
    std::optional<float> value;

    template <typename S>
    void serialize(S& s) {
        s.ext(value, bitsery::ext::InPlaceOptional(),
              [](S& s, auto& v) { s.value4b(v); });
    }
};

/**
 * Represents a call to either `getParameter` or `setParameter`, depending on
 * whether `value` contains a value or not.
 */
struct Parameter {
    using Response = ParameterResult;

    int index;
    std::optional<float> value;

    template <typename S>
    void serialize(S& s) {
        s.value4b(index);
        s.ext(value, bitsery::ext::InPlaceOptional(),
              [](S& s, auto& v) { s.value4b(v); });
    }
};

/**
 * When the host calls `processReplacing()`, `processDoubleReplacing()`, or the
 * deprecated `process()` function on our VST2 plugin, we'll write the input
 * buffers to an `AudioShmBuffer` object that's shared between the native plugin
 * an the Wine plugin host, and we'll then send this object to the Wine plugin
 * host with the rest of the .
 */
struct Vst2ProcessRequest {
    using Response = Ack;

    /**
     * The number of samples per channel. We'll trust the host to never provide
     * more samples than the maximum it indicated during `effSetBlockSize`.
     */
    int sample_frames;

    /**
     * Whether the host calling `processDoubleReplacing()` or
     * `processReplacing()`. On Linux only REAPER seems to use double precision
     * audio.
     */
    bool double_precision;

    /**
     * We'll prefetch the current transport information as part of handling an
     * audio processing call. This lets us a void an unnecessary callback (or in
     * some cases, more than one) during every processing cycle.
     */
    std::optional<VstTimeInfo> current_time_info;

    /**
     * Some plugins will also ask for the current process level during audio
     * processing. To prevent unnecessary expensive callbacks there, we'll
     * prefetch this information as well.
     */
    int current_process_level;

    /**
     * We'll periodically synchronize the realtime priority setting of the
     * host's audio thread with the Wine plugin host. We'll do this
     * approximately every ten seconds, as doing this getting and setting
     * scheduler information has a non trivial amount of overhead (even if it's
     * only a single microsoecond).
     */
    std::optional<int> new_realtime_priority;

    template <typename S>
    void serialize(S& s) {
        s.value4b(sample_frames);
        s.value1b(double_precision);

        s.ext(current_time_info, bitsery::ext::InPlaceOptional{});
        s.value4b(current_process_level);

        s.ext(new_realtime_priority, bitsery::ext::InPlaceOptional{},
              [](S& s, int& priority) { s.value4b(priority); });
    }
};

/**
 * The serialization function for `AEffect` structs. This will s serialize all
 * of the values but it will not touch any of the pointer fields. That way you
 * can deserialize to an existing `AEffect` instance. Since we can't always
 * deserialize directly into an existing `AEffect`, there is also another
 * function called `update_aeffect()` that copies values from one `AEffect` to
 * another. Both of these functions should be updating the same values.
 */
template <typename S>
void serialize(S& s, AEffect& plugin) {
    s.value4b(plugin.magic);
    s.value4b(plugin.numPrograms);
    s.value4b(plugin.numParams);
    s.value4b(plugin.numInputs);
    s.value4b(plugin.numOutputs);
    s.value4b(plugin.flags);
    s.value4b(plugin.initialDelay);
    s.value4b(plugin.empty3a);
    s.value4b(plugin.empty3b);
    s.value4b(plugin.unkown_float);
    s.value4b(plugin.uniqueID);
    s.value4b(plugin.version);
}

template <typename S>
void serialize(S& s, VstIOProperties& props) {
    s.container1b(props.data);
}

template <typename S>
void serialize(S& s, VstMidiKeyName& key_name) {
    s.container1b(key_name.data);
}

template <typename S>
void serialize(S& s, VstParameterProperties& props) {
    s.value4b(props.stepFloat);
    s.value4b(props.smallStepFloat);
    s.value4b(props.largeStepFloat);
    s.container1b(props.label);
    s.value4b(props.flags);
    s.value4b(props.minInteger);
    s.value4b(props.maxInteger);
    s.value4b(props.stepInteger);
    s.value4b(props.largeStepInteger);
    s.container1b(props.shortLabel);
    s.value2b(props.displayIndex);
    s.value2b(props.category);
    s.value2b(props.numParametersInCategory);
    s.value2b(props.reserved);
    s.container1b(props.categoryLabel);
    s.container1b(props.future);
}

template <typename S>
void serialize(S& s, VstPatchChunkInfo& info) {
    s.value4b(info.version);
    s.value4b(info.pluginUniqueID);
    s.value4b(info.pluginVersion);
    s.value4b(info.numElements);
    s.container1b(info.future);
}

template <typename S>
void serialize(S& s, VstRect& rect) {
    s.value2b(rect.top);
    s.value2b(rect.left);
    s.value2b(rect.right);
    s.value2b(rect.bottom);
}

template <typename S>
void serialize(S& s, VstTimeInfo& time_info) {
    s.value8b(time_info.samplePos);
    s.value8b(time_info.sampleRate);
    s.value8b(time_info.nanoSeconds);
    s.value8b(time_info.ppqPos);
    s.value8b(time_info.tempo);
    s.value8b(time_info.barStartPos);
    s.value8b(time_info.cycleStartPos);
    s.value8b(time_info.cycleEndPos);
    s.value4b(time_info.timeSigNumerator);
    s.value4b(time_info.timeSigDenominator);
    s.container1b(time_info.empty3);
    s.value4b(time_info.flags);
}
