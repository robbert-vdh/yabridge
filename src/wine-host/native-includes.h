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

// Libraries like Boost and bitsery think we're compiling on Windows or using a
// MSVC toolchain. This will cause them to make assumptions about the way
// certain types are defined, which headers are available and which features to
// disable (i.e. POSIX specific features). The only way around this I could
// think of was to just temporarily undefine the macros these libraries use to
// detect it's running under a WIN32 environment. If anyone knows a better way
// to do this, please let me know!

#pragma push_macro("WIN32")
#pragma push_macro("_WIN32")
#pragma push_macro("__WIN32__")
#pragma push_macro("_WIN64")

#undef WIN32
#undef _WIN32
#undef __WIN32__
#undef _WIN64

#include <bitsery/bitsery.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/process/environment.hpp>
#include <fstream>

#pragma pop_macro("WIN32")
#pragma pop_macro("_WIN32")
#pragma pop_macro("__WIN32__")
#pragma pop_macro("_WIN64")
