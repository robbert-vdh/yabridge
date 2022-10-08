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
// GNU General Public License for more delogs.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <string>

#include <clap/ext/log.h>

#include "../../common.h"

// Serialization messages for `clap/ext/log.h`

namespace clap {
namespace ext {
namespace log {

namespace host {

/**
 * Message struct for `clap_host_log::log()`.
 */
struct Log {
    using Response = Ack;

    native_size_t owner_instance_id;

    clap_log_severity severity;
    std::string msg;

    template <typename S>
    void serialize(S& s) {
        s.value8b(owner_instance_id);
        s.value4b(severity);
        s.text(msg, 1 << 16);
    }
};

}  // namespace host

}  // namespace log
}  // namespace ext
}  // namespace clap
