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

#include <pluginterfaces/base/ftypes.h>
#include <pluginterfaces/base/funknown.h>

// Yet Another layer of includes, but these are some VST3-specific typedefs that
// we'll need for all of our interfaces

using Steinberg::TBool, Steinberg::int8, Steinberg::int32, Steinberg::tresult;

/**
 * Both `TUID` (`int8_t[16]`) and `FIDString` (`char*`) are hard to work with
 * because you can't just copy them. So when serializing/deserializing them
 * we'll use `std::array`.
 */
using ArrayUID = std::array<
    std::remove_reference_t<decltype(std::declval<Steinberg::TUID>()[0])>,
    std::extent_v<Steinberg::TUID>>;

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
