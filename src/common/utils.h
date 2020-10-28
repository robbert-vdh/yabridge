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

/**
 * Return the path to the directory for story temporary files. This will be
 * `$XDG_RUNTIME_DIR` if set, and `/tmp` otherwise.
 */
boost::filesystem::path get_temporary_directory();

/**
 * Set the scheduling policy to `SCHED_FIFO` with priority 10 for this process.
 * We explicitly don't do this for wineserver itself since from my testing that
 * can actually increase latencies.
 *
 * @return Whether the operation was successful or not. This will fail if the
 *   user does not have the privileges to set realtime priorities.
 */
bool set_realtime_priority();
