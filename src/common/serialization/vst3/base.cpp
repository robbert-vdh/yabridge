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

#include "base.h"

UniversalTResult::UniversalTResult() : universal_result(Value::kResultFalse) {}

UniversalTResult::UniversalTResult(tresult native_result)
    : universal_result(to_universal_result(native_result)) {}

tresult UniversalTResult::native() const {
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
    tresult native_result) {
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
