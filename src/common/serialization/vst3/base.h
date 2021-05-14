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

#include <array>
#include <string>
#include <vector>

#include <bitsery/traits/array.h>
#include <pluginterfaces/base/ftypes.h>
#include <pluginterfaces/base/funknown.h>
#include <pluginterfaces/vst/vsttypes.h>

// Yet Another layer of includes, but these are some VST3-specific typedefs that
// we'll need for all of our interfaces

using Steinberg::TBool, Steinberg::char16, Steinberg::int8, Steinberg::int16,
    Steinberg::int32, Steinberg::int64, Steinberg::uint8, Steinberg::uint16,
    Steinberg::uint32, Steinberg::uint64, Steinberg::tresult;

/**
 * The maximum number of speakers or busses we support.
 */
constexpr size_t max_num_speakers = 16384;

/**
 * The maximum size for an `IBStream` we can serialize. Allows for up to 50 MB
 * of preset data. Hopefully no plugin will come anywhere near this limit, but
 * it will add up when plugins start to include audio samples in their presets.
 */
constexpr size_t max_vector_stream_size = 50 << 20;

/**
 * Format a FUID as a simple hexadecimal four-tuple.
 */
std::string format_uid(const Steinberg::FUID& uid);

/**
 * Convert a UTF-16 C-style string to an `std::u16string`. Who event invented
 * UTF-16?
 */
std::u16string tchar_pointer_to_u16string(const Steinberg::Vst::TChar* string);

/**
 * Same as the above, but with a fixed string length.
 *
 * @overload
 */
std::u16string tchar_pointer_to_u16string(const Steinberg::Vst::TChar* string,
                                          uint32 length);

/**
 * Convert an `std::u16string` back to a null terminated `TChar*` string.
 */
const Steinberg::Vst::TChar* u16string_to_tchar_pointer(
    const std::u16string& string) noexcept;

/**
 * Empty struct for when we have send a response to some operation without any
 * result values.
 */
struct Ack {
    template <typename S>
    void serialize(S&) {}
};

/**
 * Both `TUID` (`int8_t[16]`) and `FIDString` (`char*`) are hard to work with
 * because you can't just copy them. So when serializing/deserializing them
 * we'll use `std::array`.
 */
using ArrayUID = std::array<
    std::remove_reference_t<decltype(std::declval<Steinberg::TUID>()[0])>,
    std::extent_v<Steinberg::TUID>>;

/**
 * Store a serializable UID in the format used on the Wine host. This then has
 * to be converted to the correct native format on the plugin side.
 *
 * NOTE: This is crucial. The `INLINE_UID` macro from the VST3 SDK uses
 *       different byte ordering on Windows (with COM support) versus on other
 *       platforms. We need to reverse this transformation manually in order for
 *       projects with the Windows VST3 version of plugin X, the Linux VST3
 *       version of plugin X, and the Windows VST3 version of plugin X running
 *       through yabridge to be compatible.
 */
class WineUID {
   public:
    WineUID() noexcept;
    WineUID(const Steinberg::TUID& tuid) noexcept;

    ArrayUID get_native_uid() const noexcept;

    template <typename S>
    void serialize(S& s) {
        s.container1b(uid);
    }

   protected:
    ArrayUID uid;
};

/**
 * Store a serializable UID in the 'real' format as used by the Windows version
 * of the VST3 plugin on Windows and the Linux version of the same plugin on
 * Linux. This then has to be converted to the format reported by the plugin on
 * the Wine host.
 *
 * NOTE: This is crucial. The `INLINE_UID` macro from the VST3 SDK uses
 *       different byte ordering on Windows (with COM support) versus on other
 *       platforms. We need to reverse this transformation manually in order for
 *       projects with the Windows VST3 version of plugin X, the Linux VST3
 *       version of plugin X, and the Windows VST3 version of plugin X running
 *       through yabridge to be compatible.
 */
class NativeUID {
   public:
    NativeUID() noexcept;
    NativeUID(const Steinberg::TUID& tuid) noexcept;

    /**
     * Convert to the garbled byte order used in the Wine plugin host.
     */
    ArrayUID get_wine_uid() const noexcept;

    /**
     * Get a reference to the proper native UID.
     */
    inline const ArrayUID& native_uid() const noexcept { return uid; };

    template <typename S>
    void serialize(S& s) {
        s.container1b(uid);
    }

   protected:
    ArrayUID uid;
};

/**
 * A simple wrapper around primitive values for serialization purposes. Bitsery
 * doesn't seem to like serializing plain primitives using `s.object()` even if
 * you define a serialization function.
 */
template <typename T>
class PrimitiveWrapper {
   public:
    PrimitiveWrapper() noexcept {}
    PrimitiveWrapper(T value) noexcept : value(value) {}

    operator T() const noexcept { return value; }

    template <typename S>
    void serialize(S& s) {
        s.template value<sizeof(T)>(value);
    }

   private:
    T value;
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
    UniversalTResult() noexcept;

    /**
     * Convert a native tresult into a univeral one.
     */
    UniversalTResult(tresult native_result) noexcept;

    /**
     * Get the native equivalent for the wrapped `tresult` value.
     */
    operator tresult() const noexcept;

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

    static Value to_universal_result(tresult native_result) noexcept;

    Value universal_result;
};
