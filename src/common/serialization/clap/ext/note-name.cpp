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

#include "note-name.h"

#include "../../../utils.h"

namespace clap {
namespace ext {
namespace note_name {

NoteName::NoteName(const clap_note_name_t& original)
    : name(original.name),
      port(original.port),
      key(original.key),
      channel(original.channel) {}

void NoteName::reconstruct(clap_note_name_t& note_name) const {
    note_name = clap_note_name_t{};
    strlcpy_buffer<sizeof(note_name.name)>(note_name.name, name);
    note_name.port = port;
    note_name.key = key;
    note_name.channel = channel;
}

}  // namespace note_name
}  // namespace ext
}  // namespace clap
