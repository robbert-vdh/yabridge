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

#include <cassert>
#include <stdexcept>

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

VectorStream::VectorStream(){FUNKNOWN_CTOR}

VectorStream::VectorStream(Steinberg::IBStream* stream) {
    FUNKNOWN_CTOR

    if (!stream) {
        throw std::runtime_error("Null pointer passed to VectorStream()");
    }

    if (stream->seek(0, Steinberg::IBStream::IStreamSeekMode::kIBSeekEnd) !=
        Steinberg::kResultOk) {
        throw std::runtime_error(
            "IBStream passed to VectorStream() does not suport seeking to end");
    }

    // Now that we're at the end of the stream we know how large the buffer
    // should be
    int64 size;
    assert(stream->tell(&size) == Steinberg::kResultOk);

    int32 num_bytes_read;
    buffer.resize(size);
    assert(stream->seek(0, Steinberg::IBStream::IStreamSeekMode::kIBSeekSet) !=
           Steinberg::kResultOk);
    assert(stream->read(buffer.data(), size, &num_bytes_read) ==
           Steinberg::kResultOk);
    assert(num_bytes_read == 0 || num_bytes_read == size);
}

VectorStream::~VectorStream() {
    FUNKNOWN_DTOR
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
IMPLEMENT_REFCOUNT(VectorStream)
#pragma GCC diagnostic pop

tresult PLUGIN_API VectorStream::queryInterface(Steinberg::FIDString _iid,
                                                void** obj) {
    QUERY_INTERFACE(_iid, obj, Steinberg::FUnknown::iid, Steinberg::IBStream)
    QUERY_INTERFACE(_iid, obj, Steinberg::IBStream::iid, Steinberg::IBStream)
    QUERY_INTERFACE(_iid, obj, Steinberg::ISizeableStream::iid,
                    Steinberg::ISizeableStream)

    *obj = nullptr;
    return Steinberg::kNoInterface;
}

size_t VectorStream::size() const {
    return buffer.size();
}

tresult PLUGIN_API VectorStream::read(void* buffer,
                                      int32 numBytes,
                                      int32* numBytesRead) {
    if (!buffer || numBytes < 0) {
        return Steinberg::kInvalidArgument;
    }

    size_t bytes_to_read = std::min(static_cast<size_t>(numBytes),
                                    this->buffer.size() - seek_position);

    std::copy_n(&this->buffer[seek_position], bytes_to_read,
                reinterpret_cast<uint8_t*>(buffer));

    seek_position += bytes_to_read;
    if (numBytesRead) {
        *numBytesRead = bytes_to_read;
    }

    return Steinberg::kResultOk;
}

tresult PLUGIN_API VectorStream::write(void* buffer,
                                       int32 numBytes,
                                       int32* numBytesWritten) {
    if (!buffer || numBytes < 0) {
        return Steinberg::kInvalidArgument;
    }

    if (seek_position + numBytes > this->buffer.size()) {
        this->buffer.resize(seek_position + numBytes);
    }

    std::copy_n(reinterpret_cast<uint8_t*>(buffer), numBytes,
                &this->buffer[seek_position]);

    seek_position += numBytes;
    if (numBytesWritten) {
        *numBytesWritten = numBytes;
    }

    return Steinberg::kResultOk;
}

tresult PLUGIN_API VectorStream::seek(int64 pos, int32 mode, int64* result) {
    switch (mode) {
        case kIBSeekSet:
            seek_position = pos;
            break;
        case kIBSeekCur:
            seek_position += pos;
            break;
        case kIBSeekEnd:
            seek_position = this->buffer.size() + pos;
            break;
        default:
            return Steinberg::kInvalidArgument;
            break;
    }

    if (result) {
        *result = seek_position;
    }

    return Steinberg::kResultOk;
}

tresult PLUGIN_API VectorStream::tell(int64* pos) {
    if (pos) {
        *pos = seek_position;
        return Steinberg::kResultOk;
    } else {
        return Steinberg::kInvalidArgument;
    }
}

tresult PLUGIN_API VectorStream::getStreamSize(int64& size) {
    size = seek_position;
    return Steinberg::kResultOk;
}

tresult PLUGIN_API VectorStream::setStreamSize(int64 size) {
    buffer.resize(size);
    return Steinberg::kResultOk;
}
