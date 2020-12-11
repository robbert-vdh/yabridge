// yabridge: a Wine VST bridge
// Copyright (C) 2020  Robbert van der Helm
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

#include <array>

#include <pluginterfaces/base/ftypes.h>
#include <pluginterfaces/base/funknown.h>

// Yet Another layer of includes, but these are some VST3-specific typedefs that
// we'll need for all of our interfaces

using Steinberg::TBool, Steinberg::int8, Steinberg::int32, Steinberg::tresult;

/**
 * Both `TUID` (`int8_t[16]`) and `FIDString` (`char*`) are hard to work with
 * because you can't just copy them. So when serializing/deserializing them
 * we'll use `std::array`.
 */
using ArrayUID = std::array<
    std::remove_reference_t<decltype(std::declval<Steinberg::TUID>()[0])>,
    std::extent_v<Steinberg::TUID>>;

/**
 * Empty struct for when we have send a response to some operation without any
 * result values.
 */
struct Ack {
    template <typename S>
    void serialize(S&) {}
};
