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

#include "linking.h"

#include <cassert>

#include <dlfcn.h>

namespace fs = ghc::filesystem;

fs::path get_this_file_location() {
    // We'll try to find the library this function was defined in. When called
    // from a copy of `libyabridge-*.so` this will return that library. Because
    // the chainloader libraries load the plugin libraries from fixed locations,
    // the plugin libraries cannot use this function directly when using the
    // chainloaders.

    // On success this returns a non-zero value, just to keep you on your toes
    Dl_info info;
    assert(dladdr(reinterpret_cast<void*>(get_this_file_location), &info) != 0);
    assert(info.dli_fname);

    std::string this_file(info.dli_fname);

    // HACK: Not sure why, but `boost::dll::this_line_location()` used to return
    //       a path starting with a double slash on some systems. I've seen this
    //       happen on both Ubuntu 18.04 and 20.04, but not on Arch based
    //       distros.  Under Linux a path starting with two slashes is treated
    //       the same as a path starting with only a single slash, but Wine will
    //       refuse to load any files when the path starts with two slashes. The
    //       easiest way to work around this if this happens is to just add
    //       another leading slash and then normalize the path, since three or
    //       more slashes will be coerced into a single slash. We no longer use
    //       Boost.Dll, but unless this was an obscure Boost.Filesystem bug it
    //       sounds more likely that it was caused by some `ld.so` setting.
    //       Unless we can really figure out what was causing this, it seems
    //       best to still account for it
    if (this_file.starts_with("//")) {
        const size_t path_start_pos = this_file.find_first_not_of('/');
        if (path_start_pos != std::string::npos) {
            this_file = "/" + this_file.substr(path_start_pos);
        }
    }

    return this_file;
}
