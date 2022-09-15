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
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "params.h"

#include "../../../utils.h"

namespace clap {
namespace ext {
namespace params {

ParamInfo::ParamInfo(const clap_param_info_t& original)
    : id(original.id),
      flags(original.flags),
      cookie(reinterpret_cast<size_t>(original.cookie)),
      name(original.name),
      module(original.module),
      min_value(original.min_value),
      max_value(original.max_value),
      default_value(original.default_value) {}

void ParamInfo::reconstruct(clap_param_info_t& port_info) const {
    port_info = clap_param_info_t{};
    port_info.id = id;
    port_info.flags = flags;
    port_info.cookie = reinterpret_cast<void*>(static_cast<size_t>(cookie));
    strlcpy_buffer<sizeof(port_info.name)>(port_info.name, name);
    strlcpy_buffer<sizeof(port_info.module)>(port_info.module, module);
    port_info.min_value = min_value;
    port_info.max_value = max_value;
    port_info.default_value = default_value;
}

}  // namespace params
}  // namespace ext
}  // namespace clap
