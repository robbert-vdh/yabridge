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

#pragma once

#include <vector>

#include <bitsery/traits/vector.h>
#include <clap/stream.h>

// Serialization messages for `clap/stream.h`

namespace clap {
namespace stream {

/**
 * A serialization wrapper around streams that can be used as both a
 * `clap_istream_t` and a `clap_ostream_t`.
 */
class Stream {
   public:
    /**
     * Create an empty stream that can be written to by the plugin using
     * `ostream()`, and then written back to the host using
     * `write_to_ostream()`.
     */
    Stream();

    /**
     * Read a `clap_istream_t` from the host to a buffer. The results are
     * written to a buffer that can be serialized and send to the other side.
     */
    Stream(const clap_istream_t& original);

    /**
     * Get a `clap_ostream_t` for this buffer that the plugin can write to. This
     * is only valid as long as this object is not moved.
     */
    const clap_ostream_t* ostream();
    /**
     * Get a `clap_istream_t` for this buffer that the plugin can read the
     * buffer from. This is only valid as long as this object is not moved.
     */
    const clap_istream_t* istream();

    /**
     * Write the entire buffer to a host provided `clap_ostream_t`.
     */
    void write_to_stream(const clap_ostream_t& original) const;

    template <typename S>
    void serialize(S& s) {
        s.container1b(buffer_, 50 << 20);
    }

   protected:
    static int64_t CLAP_ABI ostream_write(const struct clap_ostream* stream,
                                          const void* buffer,
                                          uint64_t size);

    static int64_t CLAP_ABI istream_read(const struct clap_istream* stream,
                                         void* buffer,
                                         uint64_t size);

   private:
    std::vector<uint8_t> buffer_;

    /**
     * The current position in the buffer used in `istream_read()`.
     */
    size_t read_pos = 0;

    // These are populated in the `ostream()` and `istream()` methods
    clap_ostream_t ostream_vtable{};
    clap_istream_t istream_vtable{};
};

}  // namespace stream
}  // namespace clap
