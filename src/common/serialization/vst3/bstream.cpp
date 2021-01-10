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

#include <pluginterfaces/vst/vstpresetkeys.h>

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
    if (stream->seek(0, Steinberg::IBStream::IStreamSeekMode::kIBSeekEnd) ==
        Steinberg::kResultOk) {
        // Now that we're at the end of the stream we know how large the buffer
        // should be
        int64 size;
        assert(stream->tell(&size) == Steinberg::kResultOk);

        int32 num_bytes_read = 0;
        buffer.resize(size);
        assert(
            stream->seek(0, Steinberg::IBStream::IStreamSeekMode::kIBSeekSet) ==
            Steinberg::kResultOk);
        assert(stream->read(buffer.data(), size, &num_bytes_read) ==
               Steinberg::kResultOk);
        assert(num_bytes_read == 0 || num_bytes_read == size);
    }

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

        // Copy over all predefined meta data keys. `IAttributeList` does not
        // offer any interface to enumerate over the stored keys.
        // TODO: There's also
        //       `Steinberg::Vst::PresetAttributes::kFilePathStringType`
        //       This would require translating between Windows and Unix style
        //       paths, which we can't easily do outside of Wine. If this ends
        //       up being important, then we'll have to shell out to `winepath`
        //       which is not ideal. On the Wine side we can just use the
        //       `wine_get_dos_file_name` and `wine_get_unix_file_name`
        //       functions instead. Requesting this should also use a 1024
        //       character buffer.
        attributes.emplace();
        Steinberg::IPtr<Steinberg::Vst::IAttributeList> stream_attributes_list =
            stream_attributes->getAttributes();
        for (const auto& key :
             {Steinberg::Vst::PresetAttributes::kPlugInName,
              Steinberg::Vst::PresetAttributes::kPlugInCategory,
              Steinberg::Vst::PresetAttributes::kInstrument,
              Steinberg::Vst::PresetAttributes::kStyle,
              Steinberg::Vst::PresetAttributes::kCharacter,
              Steinberg::Vst::PresetAttributes::kStateType,
              Steinberg::Vst::PresetAttributes::kName,
              Steinberg::Vst::PresetAttributes::kFileName}) {
            vst_string[0] = 0;
            if (stream_attributes_list->getString(key, vst_string,
                                                  sizeof(vst_string)) ==
                Steinberg::kResultOk) {
                attributes->setString(key, vst_string);
            }
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
    if (stream->write(const_cast<uint8_t*>(buffer.data()), buffer.size(),
                      &num_bytes_written) == Steinberg::kResultOk) {
        // Some implementations will return `kResultFalse` when writing 0 bytes
        assert(num_bytes_written == 0 ||
               static_cast<size_t>(num_bytes_written) == buffer.size());
    }

    // TODO: Can plugins write their own meta data, and should we write those
    //       back after `*::getState()`? I'd assume so, but the docs don't
    //       mention this. If so then we need to always store whether the host
    //       supports `IStreamAttributes`.

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
        *numBytesRead = bytes_to_read;
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
                this->buffer.begin() + seek_position);

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
        *result = seek_position;
    }

    return Steinberg::kResultOk;
}

tresult PLUGIN_API YaBStream::tell(int64* pos) {
    if (pos) {
        *pos = seek_position;
        return Steinberg::kResultOk;
    } else {
        return Steinberg::kInvalidArgument;
    }
}

tresult PLUGIN_API YaBStream::getStreamSize(int64& size) {
    size = seek_position;
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
