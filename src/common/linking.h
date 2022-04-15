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

// This header is completely standalone so the chainloading libraries can
// retrieve their file path without pulling in a lot of additional dependencies

#include <string>

/**
 * Return a path to this `.so` file. This can be used to find out from where
 * this copy of `libyabridge-{vst2,vst3}.so` or `libyabridge-chainloader-*.so`
 * was loaded so we can search for a matching Windows plugin library. When the
 * chainloaders are used, this path should be passed to the chainloaded plugin
 * library using the provided exported functions since they can't detect the
 * path themselves.
 */
std::string get_this_file_location();
