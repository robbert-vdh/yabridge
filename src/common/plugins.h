// yabridge: a Wine VST bridge
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

#pragma once

#ifdef __WINE__
#include "../wine-host/boost-fix.h"
#endif
#include <boost/filesystem.hpp>

// Utilities and tags for plugin types and architectures

/**
 * A tag to differentiate between 32 and 64-bit `.dll` files, used to determine
 * which host application to use.
 */
enum class LibArchitecture { dll_32, dll_64 };

/**
 * A tag to differentiate between different plugin types.
 * `plugin_tyep_to_string()` and `plugin_type_from_string()` can be used to
 * convert these values to and from strings. The string form is used as a
 * command line argument for the individual Wine host applications, and the enum
 * form is passed directly in `HostRequest`.
 *
 * The `unkonwn` tag is not used directly, but in the event that we do call
 * `plugin_type_from_string()` with some invalid value we can use it to
 * gracefully show an error message without resorting to exceptions.
 */
enum class PluginType { vst2, vst3, unknown };

template <typename S>
void serialize(S& s, PluginType& plugin_type) {
    s.value4b(plugin_type);
}

/**
 * Determine the architecture of a `.dll` file based on the file header.
 *
 * See https://docs.microsoft.com/en-us/windows/win32/debug/pe-format for more
 * information on the PE32 format.
 *
 * @param path The path to the .dll file we're going to check.
 *
 * @return The detected architecture.
 * @throw std::runtime_error If the file is not a .dll file.
 */
LibArchitecture find_dll_architecture(const boost::filesystem::path&);

PluginType plugin_type_from_string(const std::string& plugin_type) noexcept;
std::string plugin_type_to_string(const PluginType& plugin_type);
