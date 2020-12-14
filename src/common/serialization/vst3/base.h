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

#include <array>
#include <string>
#include <vector>

#include <pluginterfaces/base/ftypes.h>
#include <pluginterfaces/base/funknown.h>
#include <pluginterfaces/base/ibstream.h>

// Yet Another layer of includes, but these are some VST3-specific typedefs that
// we'll need for all of our interfaces

using Steinberg::TBool, Steinberg::int8, Steinberg::int32, Steinberg::int64,
    Steinberg::tresult;

/**
 * Both `TUID` (`int8_t[16]`) and `FIDString` (`char*`) are hard to work with
 * because you can't just copy them. So when serializing/deserializing them
 * we'll use `std::array`.
 */
using ArrayUID = std::array<
    std::remove_reference_t<decltype(std::declval<Steinberg::TUID>()[0])>,
    std::extent_v<Steinberg::TUID>>;

/**
 * The maximum size for an `IBStream` we can serialize. Allows for up to 50 MB
 * of preset data. Hopefully no plugin will come anywhere near this limit, but
 * it will add up when plugins start to include audio samples in their presets.
 */
constexpr size_t max_vector_stream_size = 50 << 20;

/**
 * Empty struct for when we have send a response to some operation without any
 * result values.
 */
struct Ack {
    template <typename S>
    void serialize(S&) {}
};

/**
 * A wrapper around `Steinberg::tresult` that we can safely share between the
 * native plugin and the Wine process. Depending on the platform and on whether
 * or not the VST3 SDK is compiled to be COM compatible, the result codes may
 * have three different values for the same meaning.
 */
class UniversalTResult {
   public:
    /**
     * The default constructor will initialize the value to `kResutlFalse` and
     * should only ever be used by bitsery in the serialization process.
     */
    UniversalTResult();

    /**
     * Convert a native tresult into a univeral one.
     */
    UniversalTResult(tresult native_result);

    /**
     * Get the native equivalent for the wrapped `tresult` value.
     */
    tresult native() const;

    /**
     * Get the original name for the result, e.g. `kResultOk`.
     */
    std::string string() const;

    template <typename S>
    void serialize(S& s) {
        s.value4b(universal_result);
    }

   private:
    /**
     * These are the non-COM compatible values copied from
     * `<pluginterfaces/base/funknown.hh`> The actual values h ere don't matter
     * but hopefully the compiler can be a bit smarter about it this way.
     */
    enum class Value {
        kNoInterface = -1,
        kResultOk,
        kResultTrue = kResultOk,
        kResultFalse,
        kInvalidArgument,
        kNotImplemented,
        kInternalError,
        kNotInitialized,
        kOutOfMemory
    };

    static Value to_universal_result(tresult native_result);

    Value universal_result;
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Serialize an `IBStream` into an `std::vector<uint8_t>`, and allow the
 * receiving side to use it as an `IBStream` again. `ISizeableStream` is defined
 * but then for whatever reason never used, but we'll implement it anyways.
 */
class VectorStream : public Steinberg::IBStream,
                     public Steinberg::ISizeableStream {
   public:
    VectorStream();

    /**
     * Read an existing stream.
     *
     * @throw std::runtime_error If we couldn't read from the stream.
     */
    VectorStream(Steinberg::IBStream* stream);

    virtual ~VectorStream();

    DECLARE_FUNKNOWN_METHODS

    /**
     * Write the vector buffer back to an IBStream. After writing the seek
     * position will be left at the end of the stream.
     */
    tresult write_back(Steinberg::IBStream* stream);

    /**
     * Return the buffer's, used in the logging messages.
     */
    size_t size() const;

    // From `IBstream`
    tresult PLUGIN_API read(void* buffer,
                            int32 numBytes,
                            int32* numBytesRead = nullptr) override;
    tresult PLUGIN_API write(void* buffer,
                             int32 numBytes,
                             int32* numBytesWritten = nullptr) override;
    tresult PLUGIN_API seek(int64 pos,
                            int32 mode,
                            int64* result = nullptr) override;
    tresult PLUGIN_API tell(int64* pos) override;

    // From `ISizeableStream`
    tresult PLUGIN_API getStreamSize(int64& size) override;
    tresult PLUGIN_API setStreamSize(int64 size) override;

    template <typename S>
    void serialize(S& s) {
        s.container1b(buffer, max_vector_stream_size);
        // The seek position should always be initialized at 0
    }

   private:
    std::vector<uint8_t> buffer;
    size_t seek_position;
};

#pragma GCC diagnostic pop
