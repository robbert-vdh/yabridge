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

#include <optional>

#include <pluginterfaces/base/ibstream.h>
#include <pluginterfaces/vst/ivstattributes.h>

#include "attribute-list.h"
#include "base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Serialize an `IBStream` into an `std::vector<uint8_t>`, and allow the
 * receiving side to use it as an `IBStream` again. `ISizeableStream` is defined
 * but then for whatever reason never used, but we'll implement it anyways.
 *
 * If we're copying data from an existing `IBstream` and that stream supports
 * VST 3.6.0 preset meta data, then we'll copy that meta data as well.
 */
class YaBStream : public Steinberg::IBStream,
                  public Steinberg::ISizeableStream,
                  public Steinberg::Vst::IStreamAttributes {
   public:
    /**
     * This constructor should only be used by bitsery for serialization. The
     * other constructor will check whether the `IBstream*` provided by the host
     * supports stream attributes and configures the object accordingly.
     */
    YaBStream();

    /**
     * Read an existing stream.
     *
     * @throw std::runtime_error If we couldn't read from the stream.
     */
    YaBStream(Steinberg::IBStream* stream);

    virtual ~YaBStream();

    DECLARE_FUNKNOWN_METHODS

    /**
     * Write the vector buffer back to a host provided `IBStream`. After writing
     * the seek position will be left at the end of the stream.
     */
    tresult write_back(Steinberg::IBStream* stream) const;

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

    // From `IStreamAttributes`
    tresult PLUGIN_API getFileName(Steinberg::Vst::String128 name) override;
    Steinberg::Vst::IAttributeList* PLUGIN_API getAttributes() override;

    template <typename S>
    void serialize(S& s) {
        s.container1b(buffer, max_vector_stream_size);
        // The seek position should always be initialized at 0

        s.value1b(supports_stream_attributes);
        s.ext(file_name, bitsery::ext::StdOptional{},
              [](S& s, std::u16string& name) {
                  s.text2b(name, std::extent_v<Steinberg::Vst::String128>);
              });
        s.ext(attributes, bitsery::ext::StdOptional{});
    }

    /**
     * Whether this stream supports `IStreamAttributes`. This will be true if we
     * copied a stream provided by the host that also supported meta data.
     */
    bool supports_stream_attributes = false;

    /**
     * The stream's name, if this stream supports stream attributes.
     */
    std::optional<std::u16string> file_name;

    /**
     * The stream's meta data if we've copied from a stream that supports meta
     * data.
     */
    std::optional<YaAttributeList> attributes;

   private:
    std::vector<uint8_t> buffer;
    int64_t seek_position = 0;
};

#pragma GCC diagnostic pop
