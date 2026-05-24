// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2026 Robbert van der Helm
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

#include "ara-plugin-entry.h"

#include "../../../logging/common.h"

namespace ARA {
DEF_CLASS_IID(IPlugInEntryPoint, 0x12814E54, 0xA1CE4076, 0x82B96813, 0x16950BD6)
DEF_CLASS_IID(IPlugInEntryPoint2, 0xCD9A5913, 0xC9EB46D7, 0x96CA53AD, 0xD1DB89F5)
}  // namespace ARA

YaARAPlugInEntryPoint::ConstructArgs::ConstructArgs() noexcept : supported(false) {}

YaARAPlugInEntryPoint::ConstructArgs::ConstructArgs(
    Steinberg::IPtr<Steinberg::FUnknown> object) noexcept
    : supported(Steinberg::FUnknownPtr<ARA::IPlugInEntryPoint>(object)) {
    if (supported) {
        Logger::create_exception_logger().log(
            "DETECTED ARA PLUGIN CAPABILITY (ARA::IPlugInEntryPoint)!");
    }
}

YaARAPlugInEntryPoint::YaARAPlugInEntryPoint(ConstructArgs&& args) noexcept
    : arguments_(std::move(args)) {}

YaARAPlugInEntryPoint2::ConstructArgs::ConstructArgs() noexcept : supported(false) {}

YaARAPlugInEntryPoint2::ConstructArgs::ConstructArgs(
    Steinberg::IPtr<Steinberg::FUnknown> object) noexcept
    : supported(Steinberg::FUnknownPtr<ARA::IPlugInEntryPoint2>(object)) {
    if (supported) {
        Logger::create_exception_logger().log(
            "DETECTED ARA2 PLUGIN CAPABILITY (ARA::IPlugInEntryPoint2)!");
    }
}

YaARAPlugInEntryPoint2::YaARAPlugInEntryPoint2(ConstructArgs&& args) noexcept
    : arguments_(std::move(args)) {}
