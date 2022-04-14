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

// Libraries like (Boost.)Asio think we're compiling on Windows or using a MSVC
// toolchain. This will cause them to make incorrect assumptions which platform
// specific features are available. The only way around this I could think of
// was to just temporarily undefine the macros these libraries use to detect
// it's running under a WIN32 environment. If anyone knows a better way to do
// this, please let me know!

#pragma push_macro("WIN32")
#pragma push_macro("_WIN32")
#pragma push_macro("__WIN32__")
#pragma push_macro("_WIN64")

#undef WIN32
#undef _WIN32
#undef __WIN32__
#undef _WIN64

// This would be the minimal include needed to get Asio to work. The commented
// out includes are the actual header that would cause compile errors if not
// included here, but including headers from the detail directory directly
// didn't sound like a great idea.

// FIXME: Remove Boost stuff
#include <boost/predef.h>
#include <asio/basic_socket_streambuf.hpp>
#include <boost/interprocess/mapped_region.hpp>
// #include <asio/asio/detail/timer_queue_ptime.hpp>
// #include <boost/interprocess/detail/workaround.hpp>

#pragma pop_macro("WIN32")
#pragma pop_macro("_WIN32")
#pragma pop_macro("__WIN32__")
#pragma pop_macro("_WIN64")
