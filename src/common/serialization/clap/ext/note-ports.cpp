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

#include "note-ports.h"

#include "../../../utils.h"

namespace clap {
namespace ext {
namespace note_ports {

NotePortInfo::NotePortInfo(const clap_note_port_info_t& original)
    : id(original.id),
      supported_dialects(original.supported_dialects),
      preferred_dialect(original.preferred_dialect),
      name(original.name) {}

void NotePortInfo::reconstruct(clap_note_port_info_t& port_info) const {
    port_info = clap_note_port_info_t{};
    port_info.id = id;
    port_info.supported_dialects = supported_dialects;
    port_info.preferred_dialect = preferred_dialect;
    strlcpy_buffer<sizeof(port_info.name)>(port_info.name, name);
}

}  // namespace note_ports
}  // namespace ext
}  // namespace clap
