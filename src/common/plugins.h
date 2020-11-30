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

#ifdef __WINE__
#include "../wine-host/boost-fix.h"
#endif
#include <boost/filesystem.hpp>

// Utilities and tags for plugin types and architectures

/**
 * A tag to differentiate between 32 and 64-bit plugins, used to determine which
 * host application to use.
 */
enum class PluginArchitecture { vst_32, vst_64 };

/**
 * Determine the architecture of a VST plugin (or rather, a .dll file) based on
 * it's header values.
 *
 * See https://docs.microsoft.com/en-us/windows/win32/debug/pe-format for more
 * information on the PE32 format.
 *
 * @param plugin_path The path to the .dll file we're going to check.
 *
 * @return The detected architecture.
 * @throw std::runtime_error If the file is not a .dll file.
 */
PluginArchitecture find_vst_architecture(boost::filesystem::path);
