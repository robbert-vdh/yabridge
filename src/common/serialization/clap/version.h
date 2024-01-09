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

#pragma once

#include <compare>

#include <clap/version.h>

/**
 * Return the minimum of the given CLAP version and the CLAP version currently
 * supported by the SDK.
 */
inline clap_version_t clamp_clap_version(clap_version_t version) {
    if (CLAP_VERSION_MAJOR < version.major ||
        (version.major == CLAP_VERSION_MAJOR &&
         (CLAP_VERSION_MINOR < version.minor ||
          (version.minor == CLAP_VERSION_MINOR &&
           CLAP_VERSION_REVISION < version.revision)))) {
        return CLAP_VERSION;
    } else {
        return version;
    }
}

template <typename S>
void serialize(S& s, clap_version_t& version) {
    s.value4b(version.major);
    s.value4b(version.minor);
    s.value4b(version.revision);
}
