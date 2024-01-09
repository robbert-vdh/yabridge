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

#include "program-list-data.h"

YaProgramListData::ConstructArgs::ConstructArgs() noexcept {}

YaProgramListData::ConstructArgs::ConstructArgs(
    Steinberg::IPtr<Steinberg::FUnknown> object) noexcept
    : supported(
          Steinberg::FUnknownPtr<Steinberg::Vst::IProgramListData>(object)) {}

YaProgramListData::YaProgramListData(ConstructArgs&& args) noexcept
    : arguments_(std::move(args)) {}
