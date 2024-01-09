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

// Helper header for including toml++ in a way that works on both native Linux
// as well as with Winelib compilation

// By default tomlplusplus is no longer headers when using the package config
// file. We don't want to link against third party shared libraries in yabridge.
#ifdef TOML_SHARED_LIB

#undef TOML_SHARED_LIB
#undef TOML_HEADER_ONLY
#define TOML_HEADER_ONLY 1

#endif

// tomlplusplus recently got some Windows fixes, but they cause compilation
// errors and we don't need them so we'll just disable them outright. Disabling
// `TOML_ENABLE_WINDOWS_COMPAT` is no longer enough, and you can't disable
// `TOML_WINDOWS` directly. This is the same trick used in `use-asio-linux.h`.
#pragma push_macro("WIN32")
#pragma push_macro("_WIN32")
#pragma push_macro("__WIN32__")
#pragma push_macro("__NT__")
#pragma push_macro("__CYGWIN__")
#undef WIN32
#undef _WIN32
#undef __WIN32__
#undef __NT__
#undef __CYGWIN__

#include <toml++/toml.h>

#pragma pop_macro("WIN32")
#pragma pop_macro("_WIN32")
#pragma pop_macro("__WIN32__")
#pragma pop_macro("__NT__")
#pragma pop_macro("__CYGWIN__")
