// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2024 Robbert van der Helm
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

YaBStream::YaBStream() noexcept {FUNKNOWN_CTOR}

YaBStream::YaBStream(Steinberg::IBStream* stream) {
    FUNKNOWN_CTOR

    if (!stream) {
        throw std::runtime_error("Null pointer passed to YaBStream()");
    }

    // Copy any existing contents, used for `IComponent::setState` and similar
    // methods
    // NOTE: Bitwig Studio seems to prepend some default header on new presets
    //       We _don't_ want to copy that, since some plugins may try to read
    //       the entire preset and fail to load. Examples of such plugins are
    //       the iZotope Rx7 plugins.
    int64 old_position;
    stream->tell(&old_position);
    if (stream->seek(0, Steinberg::IBStream::IStreamSeekMode::kIBSeekEnd) ==
        Steinberg::kResultOk) {
        int64 size = 0;
        stream->tell(&size);
        size -= old_position;

        if (size > 0) {
            int32 num_bytes_read = 0;
            buffer_.resize(size);
            stream->seek(old_position,
                         Steinberg::IBStream::IStreamSeekMode::kIBSeekSet);
            stream->read(buffer_.data(), static_cast<int32>(size),
                         &num_bytes_read);
            assert(num_bytes_read == 0 || num_bytes_read == size);
        }
    }

    // If the host did prepend some header, we should leave it in place when
    // writing
    stream->seek(old_position,
                 Steinberg::IBStream::IStreamSeekMode::kIBSeekSet);

    // Starting at VST 3.6.0 streams provided by the host may contain context
    // based meta data
    if (Steinberg::FUnknownPtr<Steinberg::Vst::IStreamAttributes>
            stream_attributes = stream) {
        supports_stream_attributes_ = true;

        Steinberg::Vst::String128 vst_string{0};
        if (stream_attributes->getFileName(vst_string) ==
            Steinberg::kResultOk) {
            file_name_.emplace(tchar_pointer_to_u16string(vst_string));
        }

        if (Steinberg::IPtr<Steinberg::Vst::IAttributeList>
                stream_attributes_list = stream_attributes->getAttributes()) {
            attributes_.emplace(YaAttributeList::read_stream_attributes(
                stream_attributes_list));
        } else {
            attributes_.emplace();
        }
    }
}

YaBStream::~YaBStream() noexcept {FUNKNOWN_DTOR}
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
    if (supports_stream_attributes_) {
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
    if (stream->write(const_cast<uint8_t*>(buffer_.data()),
                      static_cast<int32>(buffer_.size()),
                      &num_bytes_written) == Steinberg::kResultOk) {
        // Some implementations will return `kResultFalse` when writing 0 bytes
        assert(num_bytes_written == 0 ||
               static_cast<size_t>(num_bytes_written) == buffer_.size());
    }

    // Write back any attributes written by the plugin if the host supports
    // preset meta data
    if (Steinberg::FUnknownPtr<Steinberg::Vst::IStreamAttributes>
            stream_attributes = stream;
        stream_attributes && attributes_) {
        if (Steinberg::IPtr<Steinberg::Vst::IAttributeList>
                stream_attributes_list = stream_attributes->getAttributes()) {
            // XXX: If the host somehow preset some attributes, then we're also
            //      writing those back. This should not cause any issues though.
            attributes_->write_back(stream_attributes_list);
        }
    }

    return Steinberg::kResultOk;
}

size_t YaBStream::size() const noexcept {
    return buffer_.size();
}

tresult PLUGIN_API YaBStream::read(void* buffer,
                                   int32 numBytes,
                                   int32* numBytesRead) {
    if (!buffer || numBytes < 0) {
        return Steinberg::kInvalidArgument;
    }

    const int64_t bytes_to_read =
        std::min(static_cast<int64_t>(numBytes),
                 static_cast<int64_t>(buffer_.size()) - seek_position_);

    if (bytes_to_read > 0) {
        std::copy_n(&buffer_[seek_position_], bytes_to_read,
                    reinterpret_cast<uint8_t*>(buffer));
        seek_position_ += bytes_to_read;
    }

    if (numBytesRead) {
        *numBytesRead = static_cast<int32>(bytes_to_read);
    }

    return bytes_to_read > 0 ? Steinberg::kResultOk : Steinberg::kResultFalse;
}

tresult PLUGIN_API YaBStream::write(void* buffer,
                                    int32 numBytes,
                                    int32* numBytesWritten) {
    if (!buffer || numBytes < 0) {
        return Steinberg::kInvalidArgument;
    }

    if (seek_position_ + numBytes > static_cast<int64_t>(buffer_.size())) {
        buffer_.resize(seek_position_ + numBytes);
    }

    std::copy_n(reinterpret_cast<uint8_t*>(buffer), numBytes,
                &buffer_[seek_position_]);

    seek_position_ += numBytes;
    if (numBytesWritten) {
        *numBytesWritten = numBytes;
    }

    return Steinberg::kResultOk;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
tresult PLUGIN_API YaBStream::seek(int64 pos, int32 mode, int64* result) {
    switch (mode) {
        case kIBSeekSet:
            seek_position_ = pos;
            break;
        case kIBSeekCur:
            seek_position_ += pos;
            break;
        case kIBSeekEnd:
            seek_position_ = static_cast<int64_t>(buffer_.size()) + pos;
            break;
        default:
            return Steinberg::kInvalidArgument;
            break;
    }

    seek_position_ = std::clamp(seek_position_, static_cast<int64_t>(0),
                                static_cast<int64_t>(buffer_.size()));
    if (result) {
        *result = static_cast<int64>(seek_position_);
    }

    return Steinberg::kResultOk;
}

tresult PLUGIN_API YaBStream::tell(int64* pos) {
    if (pos) {
        *pos = seek_position_;
        return Steinberg::kResultOk;
    } else {
        return Steinberg::kInvalidArgument;
    }
}

tresult PLUGIN_API YaBStream::getStreamSize(int64& size) {
    size = static_cast<int64>(buffer_.size());
    return Steinberg::kResultOk;
}

tresult PLUGIN_API YaBStream::setStreamSize(int64 size) {
    buffer_.resize(size);
    return Steinberg::kResultOk;
}

tresult PLUGIN_API YaBStream::getFileName(Steinberg::Vst::String128 name) {
    if (name && file_name_) {
        std::copy(file_name_->begin(), file_name_->end(), name);
        name[file_name_->size()] = 0;

        return Steinberg::kResultOk;
    } else {
        return Steinberg::kResultFalse;
    }
}

Steinberg::Vst::IAttributeList* PLUGIN_API YaBStream::getAttributes() {
    if (attributes_) {
        return &*attributes_;
    } else {
        return nullptr;
    }
}
