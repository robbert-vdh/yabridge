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

#include <bitsery/adapter/buffer.h>
#include <bitsery/ext/pointer.h>
#include <bitsery/ext/std_optional.h>
#include <bitsery/ext/std_variant.h>
#include <bitsery/traits/array.h>
#include <bitsery/traits/string.h>
#include <bitsery/traits/vector.h>
#include <vestige/aeffectx.h>

#include <variant>

// These constants are limits used by bitsery

/**
 * The maximum number of audio channels supported.
 */
constexpr size_t max_audio_channels = 32;
/**
 * The maximum number of samples in a buffer.
 */
constexpr size_t max_buffer_size = 16384;
/**
 * The maximum number of midi events in a single `VstEvents` struct.
 *
 * TODO: Can this go higher?
 */
constexpr size_t max_midi_events = 32;
/**
 * The maximum size in bytes of a string or buffer passed through a void pointer
 * in one of the dispatch functions. This is used to create buffers for plugins
 * to write strings to.
 */
constexpr size_t max_string_length = 64;

/**
 * A simple constant sized buffer for smaller types that can be allocated on the
 * stack.
 */
template <std::size_t N>
using ArrayBuffer = std::array<uint8_t, N>;

template <typename B>
using OutputAdapter = bitsery::OutputBufferAdapter<B>;

template <typename B>
using InputAdapter = bitsery::InputBufferAdapter<B>;

// The cannonical overloading template for `std::visitor`, not sure why this
// isn't part of the standard library
template <class... Ts>
struct overload : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
overload(Ts...)->overload<Ts...>;

/**
 * A wrapper around `VstEvents` that stores the data in a vector instead of a
 * C-style array. Needed until bitsery supports C-style arrays
 * https://github.com/fraillt/bitsery/issues/28. An advantage of this approach
 * is that RAII will handle cleanup for us.
 *
 * Before serialization the events are read from a C-style array into a vector
 * using this class's constructor, and after deserializing the original struct
 * can be reconstructed usign the `as_c_events()` method.
 */
class alignas(16) DynamicVstEvents {
   public:
    DynamicVstEvents(){};

    explicit DynamicVstEvents(const VstEvents& c_events);

    /**
     * Construct a `VstEvents` struct from the events vector. This contains a
     * pointer to that vector's elements, so the returned object should not
     * outlive this struct.
     */
    VstEvents& as_c_events();

    // XXX: The original `VstEvents` stuct hasonly one C-style array of
    //      `VstEvent`s, but I've seen some implementation that have two. Is
    //      this only for alignment or does this have an actual use?
    std::vector<VstEvent> events;

   private:
    /**
     * A `VstEvents` struct based on the `events` vector. Use the
     * `as_c_events()` method to populate and return this after the `events`
     * vector has been filled.
     */
    VstEvents vst_events;
    /**
     * The `VstEvents` struct is defined to look like it contains a one or two
     * element array of `VstEvent` pointers. The actual truth is that the
     * `VstEvents::event` array is actually a variable length array with length
     * `VstEvents::numEvents`. This is probably not part of any header files
     * because VLAs are not part of any C++ standard. This struct is here to
     * make sure there is enough room to copy the elements into.
     */
    size_t dummy[max_midi_events];
};

/**
 * Marker struct to indicate that that the event requires some buffer to write
 * its results into. This is to prevent us from having to unnecessarily sending
 * around empty arrays.
 */
struct NeedsBuffer {};

/**
 * VST events are passed a void pointer that can contain a variety of different
 * data types depending on the event's opcode. This is typically either:
 *
 * - A null pointer, used for simple events.
 * - A char pointer to a null terminated string, used for passing strings to the
 *   plugin such as when renaming presets. Bitsery handles the serialization for
 *   us.
 *
 *   NOTE: Bitsery does not support null terminated C-strings without a known
 *         size. We can replace `std::string` with `char*` once it does for
 *         clarity's sake.
 *
 * - Specific data structures from `aeffextx.h`. For instance an event with the
 *   opcode `effProcessEvents` comes with a struct containing a list of midi
 *   events.
 *
 *   TODO: A lot of these are still missing
 *
 * - Some empty buffer for the plugin to write its own data to, for instance for
 *   a plugin to report its name or the label for a certain parameter. We'll
 *   assume that this is the default if none of the above options apply.
 */
using EventPayload =
    std::variant<std::nullptr_t, std::string, DynamicVstEvents, NeedsBuffer>;

/**
 * An event as dispatched by the VST host. These events will get forwarded to
 * the VST host process running under Wine. The fields here mirror those
 * arguments sent to the `AEffect::dispatch` function.
 */
struct Event {
    // TODO: Possibly use a vector here sicne we can't know the maximum size for
    //       certain
    using buffer_type = ArrayBuffer<sizeof(VstMidiEvent) * max_midi_events>;

    // Ensure that the buffer can be aligned correctly and that strings will fit
    static_assert(std::tuple_size<buffer_type>::value % 16 == 0);
    static_assert(std::tuple_size<buffer_type>::value >=
                  max_string_length + 32);

    int opcode;
    int index;
    // TODO: This is an intptr_t, if we want to support 32 bit Wine plugins all
    //       of these these intptr_t types should be replace by `uint64_t` to
    //       remain compatible with the Linux VST plugin.
    intptr_t value;
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
    EventPayload payload;

    template <typename S>
    void serialize(S& s) {
        s.value4b(opcode);
        s.value4b(index);
        // Hard coding pointer sizes to 8 bytes should be fine, right? Even if
        // we're hosting a 32 bit plugin the native VST plugin will still use 64
        // bit large pointers.
        s.value8b(value);
        s.value4b(option);

        // I couldn't get this serializer to work seperately without
        // `EventPayload` in a struct
        s.ext(payload, bitsery::ext::StdVariant{
                           [](S&, std::nullptr_t&) {},
                           [](S& s, std::string& string) {
                               s.text1b(string, max_string_length);
                           },
                           [](S& s, DynamicVstEvents& events) {
                               s.container(events.events, max_midi_events,
                                           [](S& s, VstEvent& event) {
                                               s.container1b(event.dump);
                                           });
                           },
                           [](S&, NeedsBuffer&) {}});
    }
};

/**
 * AN instance of this should be sent back as a response to an incoming event.
 */
struct EventResult {
    using buffer_type = ArrayBuffer<max_string_length + 32>;

    /**
     * The result that should be returned from the dispatch function.
     */
    intptr_t return_value;
    /**
     * If present, this should get written into the void pointer passed to the
     * dispatch function.
     */
    std::optional<std::string> data;

    template <typename S>
    void serialize(S& s) {
        s.value8b(return_value);
        s.ext(data, bitsery::ext::StdOptional(),
              [](S& s, auto& v) { s.text1b(v, max_string_length); });
    }
};

/**
 * Represents a call to either `getParameter` or `setParameter`, depending on
 * whether `value` contains a value or not.
 */
struct Parameter {
    using buffer_type = ArrayBuffer<16>;

    int index;
    std::optional<float> value;

    template <typename S>
    void serialize(S& s) {
        s.value4b(index);
        s.ext(value, bitsery::ext::StdOptional(),
              [](S& s, auto& v) { s.value4b(v); });
    }
};

/**
 * The result of a `getParameter` or a `setParameter` call. For `setParameter`
 * this struct won't contain any values and mostly acts as an acknowledgement
 * from the Wine VST host.
 */
struct ParameterResult {
    using buffer_type = ArrayBuffer<16>;

    std::optional<float> value;

    template <typename S>
    void serialize(S& s) {
        s.ext(value, bitsery::ext::StdOptional(),
              [](S& s, auto& v) { s.value4b(v); });
    }
};

/**
 * A buffer of audio for the plugin to process, or the response of that
 * processing. The number of samples is encoded in each audio buffer's length.
 */
struct AudioBuffers {
    // When sending data we could use a vector of the right size, but when
    // receiving data we don't know how large this vector should be in advance
    // (or without sending the message length first)
    using buffer_type =
        ArrayBuffer<max_audio_channels * max_buffer_size * sizeof(float) + 16>;

    /**
     * An audio buffer for each of the plugin's audio channels.
     */
    std::vector<std::vector<float>> buffers;

    /**
     * The number of frames in a sample. If buffers is not empty, then
     * `buffers[0].size() == sample_frames`.
     */
    int sample_frames;

    template <typename S>
    void serialize(S& s) {
        s.container(buffers, max_audio_channels,
                    [](S& s, auto& v) { s.container4b(v, max_buffer_size); });
        s.value4b(sample_frames);
    }
};

/**
 * The serialization function for `AEffect` structs. This will s serialize all
 * of the values but it will not touch any of the pointer fields. That way you
 * can deserialize to an existing `AEffect` instance.
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
