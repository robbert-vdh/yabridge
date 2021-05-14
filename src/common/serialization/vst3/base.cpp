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

#include <cassert>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "base.h"

std::string format_uid(const Steinberg::FUID& uid) {
    // This is the same as `FUID::print`, but without any macro prefixes
    uint32 l1, l2, l3, l4;
    uid.to4Int(l1, l2, l3, l4);

    std::ostringstream formatted_uid;
    formatted_uid << std::hex << std::uppercase << "{0x" << std::setfill('0')
                  << std::setw(8) << l1 << ", 0x" << std::setfill('0')
                  << std::setw(8) << l2 << ", 0x" << std::setfill('0')
                  << std::setw(8) << l3 << ", 0x" << std::setfill('0')
                  << std::setw(8) << l4 << "}";

    return formatted_uid.str();
}

std::u16string tchar_pointer_to_u16string(const Steinberg::Vst::TChar* string) {
#ifdef __WINE__
    // This is great, thanks Steinberg
    static_assert(sizeof(Steinberg::Vst::TChar) == sizeof(char16_t));
    return std::u16string(reinterpret_cast<const char16_t*>(string));
#else
    return std::u16string(static_cast<const char16_t*>(string));
#endif
}

std::u16string tchar_pointer_to_u16string(const Steinberg::Vst::TChar* string,
                                          uint32 length) {
#ifdef __WINE__
    static_assert(sizeof(Steinberg::Vst::TChar) == sizeof(char16_t));
    return std::u16string(reinterpret_cast<const char16_t*>(string), length);
#else
    return std::u16string(static_cast<const char16_t*>(string), length);
#endif
}

const Steinberg::Vst::TChar* u16string_to_tchar_pointer(
    const std::u16string& string) noexcept {
#ifdef __WINE__
    static_assert(sizeof(Steinberg::Vst::TChar) == sizeof(char16_t));
    return reinterpret_cast<const Steinberg::Vst::TChar*>(string.c_str());
#else
    return static_cast<const Steinberg::Vst::TChar*>(string.c_str());
#endif
}

WineUID::WineUID() noexcept {}
WineUID::WineUID(const Steinberg::TUID& tuid) noexcept
    : uid(std::to_array(tuid)) {}

ArrayUID WineUID::get_native_uid() const noexcept {
    // We need to shuffle the first 8 bytes around to convert between the
    // COM-compatible and non COM-compatible formats described by the
    // `INLINE_UID` macro. See that macro as a reference for the transformations
    // we're applying here.
    ArrayUID converted_uid = uid;

    converted_uid[0] = uid[3];
    converted_uid[1] = uid[2];
    converted_uid[2] = uid[1];
    converted_uid[3] = uid[0];

    converted_uid[4] = uid[5];
    converted_uid[5] = uid[4];
    converted_uid[6] = uid[7];
    converted_uid[7] = uid[6];

    return converted_uid;
}

NativeUID::NativeUID() noexcept {}
NativeUID::NativeUID(const Steinberg::TUID& tuid) noexcept
    : uid(std::to_array(tuid)) {}

ArrayUID NativeUID::get_wine_uid() const noexcept {
    // This transformation is actually the same as the one in
    // `WineUID::get_native_uid()`, but we'll spell it out here in full for
    // understandability's sake.
    ArrayUID converted_uid = uid;

    converted_uid[0] = uid[3];
    converted_uid[1] = uid[2];
    converted_uid[2] = uid[1];
    converted_uid[3] = uid[0];

    converted_uid[4] = uid[5];
    converted_uid[5] = uid[4];
    converted_uid[6] = uid[7];
    converted_uid[7] = uid[6];

    return converted_uid;
}

UniversalTResult::UniversalTResult() noexcept
    : universal_result(Value::kResultFalse) {}

UniversalTResult::UniversalTResult(tresult native_result) noexcept
    : universal_result(to_universal_result(native_result)) {}

UniversalTResult::operator tresult() const noexcept {
    static_assert(Steinberg::kResultOk == Steinberg::kResultTrue);
    switch (universal_result) {
        case Value::kNoInterface:
            return Steinberg::kNoInterface;
            break;
        case Value::kResultOk:
            return Steinberg::kResultOk;
            break;
        case Value::kResultFalse:
            return Steinberg::kResultFalse;
            break;
        case Value::kInvalidArgument:
            return Steinberg::kInvalidArgument;
            break;
        case Value::kNotImplemented:
            return Steinberg::kNotImplemented;
            break;
        case Value::kInternalError:
            return Steinberg::kInternalError;
            break;
        case Value::kNotInitialized:
            return Steinberg::kNotInitialized;
            break;
        case Value::kOutOfMemory:
            return Steinberg::kOutOfMemory;
            break;
        default:
            // Shouldn't be happening
            return Steinberg::kInvalidArgument;
            break;
    }
}

std::string UniversalTResult::string() const {
    static_assert(Steinberg::kResultOk == Steinberg::kResultTrue);
    switch (universal_result) {
        case Value::kNoInterface:
            return "kNoInterface";
            break;
        case Value::kResultOk:
            return "kResultOk";
            break;
        case Value::kResultFalse:
            return "kResultFalse";
            break;
        case Value::kInvalidArgument:
            return "kInvalidArgument";
            break;
        case Value::kNotImplemented:
            return "kNotImplemented";
            break;
        case Value::kInternalError:
            return "kInternalError";
            break;
        case Value::kNotInitialized:
            return "kNotInitialized";
            break;
        case Value::kOutOfMemory:
            return "kOutOfMemory";
            break;
        default:
            return "<invalid>";
            break;
    }
}

UniversalTResult::Value UniversalTResult::to_universal_result(
    tresult native_result) noexcept {
    static_assert(Steinberg::kResultOk == Steinberg::kResultTrue);
    switch (native_result) {
        case Steinberg::kNoInterface:
            return Value::kNoInterface;
            break;
        case Steinberg::kResultOk:
            return Value::kResultOk;
            break;
        case Steinberg::kResultFalse:
            return Value::kResultFalse;
            break;
        case Steinberg::kInvalidArgument:
            return Value::kInvalidArgument;
            break;
        case Steinberg::kNotImplemented:
            return Value::kNotImplemented;
            break;
        case Steinberg::kInternalError:
            return Value::kInternalError;
            break;
        case Steinberg::kNotInitialized:
            return Value::kNotInitialized;
            break;
        case Steinberg::kOutOfMemory:
            return Value::kOutOfMemory;
            break;
        default:
            // Shouldn't be happening
            return Value::kInvalidArgument;
            break;
    }
}
