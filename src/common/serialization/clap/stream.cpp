// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2022 Robbert van der Helm
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more destates.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "stream.h"

namespace clap {
namespace stream {

/**
 * We'll try to read the host's `clap_istream_t` in 1 MB chunks.
 */
constexpr size_t read_chunk_size = 1 << 20;

Stream::Stream() {}

Stream::Stream(const clap_istream_t& original) {
    // CLAP streams have no length indication. A plugin could do something like
    // prepending the stream's length to the stream, but we can't do that. So
    // instead we'll try to read in 1 MB chunks until we reach end of file. Even
    // if the stream's size is over 1 MB, the host may still return less than 1
    // MB at a time at its discretion.
    size_t stream_length = 0;
    while (true) {
        // Start by reserving enough capacity to read 1 MB
        buffer_.resize(stream_length + read_chunk_size);
        const int64_t num_bytes_read = original.read(
            &original, buffer_.data() + stream_length, read_chunk_size);

        // We're done when we reach the end of the file
        if (num_bytes_read <= 0) {
            break;
        }

        stream_length += num_bytes_read;
    }

    // Trim the excess reserved space
    buffer_.resize(stream_length);
}

const clap_ostream_t* Stream::ostream() {
    ostream_vtable_.write = ostream_write;
    ostream_vtable_.ctx = this;

    return &ostream_vtable_;
}

const clap_istream_t* Stream::istream() {
    istream_vtable_.read = istream_read;
    istream_vtable_.ctx = this;

    return &istream_vtable_;
}

void Stream::write_to_stream(const clap_ostream_t& original) const {
    // The host may not let us write the whole stream all at once, so we need to
    // keep track of how many bytes we've written and keep going until
    // everything has been written back to the host.
    size_t num_bytes_written = 0;
    while (num_bytes_written < buffer_.size()) {
        const int64_t actual_written_bytes =
            original.write(&original, buffer_.data() + num_bytes_written,
                           buffer_.size() - num_bytes_written);
        assert(actual_written_bytes > 0);

        num_bytes_written += actual_written_bytes;
    }
}

int64_t CLAP_ABI Stream::ostream_write(const struct clap_ostream* stream,
                                       const void* buffer,
                                       uint64_t size) {
    assert(stream && stream->ctx && buffer);
    auto self = static_cast<Stream*>(stream->ctx);

    // We can just read everything at the same time
    const size_t start_pos = self->buffer_.size();
    self->buffer_.resize(start_pos + size);
    std::copy_n(static_cast<const uint8_t*>(buffer), size,
                self->buffer_.data() + start_pos);

    return static_cast<int64_t>(size);
}

int64_t CLAP_ABI Stream::istream_read(const struct clap_istream* stream,
                                      void* buffer,
                                      uint64_t size) {
    assert(stream && stream->ctx && buffer);
    auto self = static_cast<Stream*>(stream->ctx);

    // `self->read_pos` is a cursor in the buffer. CLAP streams always read from
    // begin to end with no way to rewind.
    const size_t num_bytes_read = std::min(
        static_cast<size_t>(size), self->buffer_.size() - self->read_pos_);

    std::copy_n(self->buffer_.data() + self->read_pos_, num_bytes_read,
                static_cast<uint8_t*>(buffer));
    self->read_pos_ += num_bytes_read;

    return static_cast<int64_t>(num_bytes_read);
}

}  // namespace stream
}  // namespace clap
