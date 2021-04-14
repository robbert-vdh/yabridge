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

#include "bstream.h"

#include <cassert>
#include <stdexcept>

YaBStream::YaBStream(){FUNKNOWN_CTOR}

YaBStream::YaBStream(Steinberg::IBStream* stream) {
    FUNKNOWN_CTOR

    if (!stream) {
        throw std::runtime_error("Null pointer passed to YaBStream()");
    }

    // Copy any existing contents, used for `IComponent::setState` and similar
    // methods
    int64 old_position;
    stream->tell(&old_position);
    if (stream->seek(0, Steinberg::IBStream::IStreamSeekMode::kIBSeekEnd) ==
        Steinberg::kResultOk) {
        int64 size = 0;
        stream->tell(&size);
        if (size > 0) {
            int32 num_bytes_read = 0;
            buffer.resize(size);
            stream->seek(0, Steinberg::IBStream::IStreamSeekMode::kIBSeekSet);
            stream->read(buffer.data(), static_cast<int32>(size),
                         &num_bytes_read);
            assert(num_bytes_read == 0 || num_bytes_read == size);
        }
    }
    stream->seek(old_position,
                 Steinberg::IBStream::IStreamSeekMode::kIBSeekSet);

    // Starting at VST 3.6.0 streams provided by the host may contain context
    // based meta data
    if (Steinberg::FUnknownPtr<Steinberg::Vst::IStreamAttributes>
            stream_attributes = stream) {
        supports_stream_attributes = true;

        Steinberg::Vst::String128 vst_string{0};
        if (stream_attributes->getFileName(vst_string) ==
            Steinberg::kResultOk) {
            file_name.emplace(tchar_pointer_to_u16string(vst_string));
        }

        if (Steinberg::IPtr<Steinberg::Vst::IAttributeList>
                stream_attributes_list = stream_attributes->getAttributes()) {
            attributes.emplace(YaAttributeList::read_stream_attributes(
                stream_attributes_list));
        } else {
            attributes.emplace();
        }
    }
}

YaBStream::~YaBStream() {
    FUNKNOWN_DTOR
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
IMPLEMENT_REFCOUNT(YaBStream)
#pragma GCC diagnostic pop

tresult PLUGIN_API YaBStream::queryInterface(Steinberg::FIDString _iid,
                                             void** obj) {
    QUERY_INTERFACE(_iid, obj, Steinberg::FUnknown::iid, Steinberg::IBStream)
    QUERY_INTERFACE(_iid, obj, Steinberg::IBStream::iid, Steinberg::IBStream)
    QUERY_INTERFACE(_iid, obj, Steinberg::ISizeableStream::iid,
                    Steinberg::ISizeableStream)

    // TODO: We don't have any logging for this
    if (supports_stream_attributes) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IStreamAttributes::iid,
                        Steinberg::Vst::IStreamAttributes)
    }

    *obj = nullptr;
    return Steinberg::kNoInterface;
}

tresult YaBStream::write_back(Steinberg::IBStream* stream) const {
    if (!stream) {
        return Steinberg::kInvalidArgument;
    }

    // A `stream->seek(0, kIBSeekSet)` breaks restoring states in Bitwig. Not
    // sure if Bitwig is prepending a header or if this is expected behaviour.
    int32 num_bytes_written = 0;
    if (stream->write(const_cast<uint8_t*>(buffer.data()),
                      static_cast<int32>(buffer.size()),
                      &num_bytes_written) == Steinberg::kResultOk) {
        // Some implementations will return `kResultFalse` when writing 0 bytes
        assert(num_bytes_written == 0 ||
               static_cast<size_t>(num_bytes_written) == buffer.size());
    }

    // Write back any attributes written by the plugin if the host supports
    // preset meta data
    if (Steinberg::FUnknownPtr<Steinberg::Vst::IStreamAttributes>
            stream_attributes = stream;
        stream_attributes && attributes) {
        if (Steinberg::IPtr<Steinberg::Vst::IAttributeList>
                stream_attributes_list = stream_attributes->getAttributes()) {
            // XXX: If the host somehow preset some attributes, then we're also
            //      writing those back. This should not cause any issues though.
            attributes->write_back(stream_attributes_list);
        }
    }

    return Steinberg::kResultOk;
}

size_t YaBStream::size() const {
    return buffer.size();
}

tresult PLUGIN_API YaBStream::read(void* buffer,
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
        *numBytesRead = static_cast<int32>(bytes_to_read);
    }

    return Steinberg::kResultOk;
}

tresult PLUGIN_API YaBStream::write(void* buffer,
                                    int32 numBytes,
                                    int32* numBytesWritten) {
    if (!buffer || numBytes < 0) {
        return Steinberg::kInvalidArgument;
    }

    if (seek_position + numBytes > this->buffer.size()) {
        this->buffer.resize(seek_position + numBytes);
    }

    std::copy_n(reinterpret_cast<uint8_t*>(buffer), numBytes,
                this->buffer.begin() + static_cast<int>(seek_position));

    seek_position += numBytes;
    if (numBytesWritten) {
        *numBytesWritten = numBytes;
    }

    return Steinberg::kResultOk;
}

tresult PLUGIN_API YaBStream::seek(int64 pos, int32 mode, int64* result) {
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
        *result = static_cast<int64>(seek_position);
    }

    return Steinberg::kResultOk;
}

tresult PLUGIN_API YaBStream::tell(int64* pos) {
    if (pos) {
        *pos = static_cast<int64>(seek_position);
        return Steinberg::kResultOk;
    } else {
        return Steinberg::kInvalidArgument;
    }
}

tresult PLUGIN_API YaBStream::getStreamSize(int64& size) {
    size = static_cast<int64>(seek_position);
    return Steinberg::kResultOk;
}

tresult PLUGIN_API YaBStream::setStreamSize(int64 size) {
    buffer.resize(size);
    return Steinberg::kResultOk;
}

tresult PLUGIN_API YaBStream::getFileName(Steinberg::Vst::String128 name) {
    if (name && file_name) {
        std::copy(file_name->begin(), file_name->end(), name);
        name[file_name->size()] = 0;

        return Steinberg::kResultOk;
    } else {
        return Steinberg::kResultFalse;
    }
}

Steinberg::Vst::IAttributeList* PLUGIN_API YaBStream::getAttributes() {
    if (attributes) {
        return &*attributes;
    } else {
        return nullptr;
    }
}
