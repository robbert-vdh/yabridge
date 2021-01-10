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

#include <pluginterfaces/base/ibstream.h>

#include "base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Serialize an `IBStream` into an `std::vector<uint8_t>`, and allow the
 * receiving side to use it as an `IBStream` again. `ISizeableStream` is defined
 * but then for whatever reason never used, but we'll implement it anyways.
 */
class YaBStream : public Steinberg::IBStream,
                  public Steinberg::ISizeableStream {
   public:
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
     * Write the vector buffer back to an IBStream. After writing the seek
     * position will be left at the end of the stream.
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

    template <typename S>
    void serialize(S& s) {
        s.container1b(buffer, max_vector_stream_size);
        // The seek position should always be initialized at 0
    }

   private:
    std::vector<uint8_t> buffer;
    size_t seek_position = 0;
};

#pragma GCC diagnostic pop
