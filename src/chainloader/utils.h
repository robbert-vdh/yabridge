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

#pragma once

#include <string>

/**
 * Finds the matching `libyabridge-*.so` for this chainloader. Returns the
 * handle if it is found. Otherwise, we'll log an error and show a desktop
 * notification, and this function returns a null pointer. The pointer may be
 * `dlclose()`'d when it's no longer needed. This search works in the following
 * order:
 *
 * - First we'll try to locate `yabridge-host.exe` using the same method used by
 *   the yabridge plugin bridges themselves. We'll search in `$PATH`, followed
 *   by `${XDG_DATA_HOME:-$HOME/.local/share}/yabridge`. If that file exists and
 *   the target plugin library exists right next to it, then we'll use that.
 * - For compatibility with 32-bit only builds of yabridge, we'll repeat this
 *   process for `yabridge-host-32.exe`.
 * - When those don't exist, we'll try to `dlopen()` the file directly. This
 *   will use the correct path for the system.
 * - If we still can't find the file, we'll do one last scan through common lib
 *   directories in case `ldconfig` was not set up correctly.
 */
void* find_plugin_library(const std::string& name);
