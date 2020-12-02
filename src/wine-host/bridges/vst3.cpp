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

#include "vst3.h"

#include "../boost-fix.h"

// TODO: Do something with this, I just wanted to get the build working
#include <public.sdk/source/vst/hosting/module_win32.cpp>

void justdewit(const std::string& path) {
    std::string error;
    std::shared_ptr<VST3::Hosting::Module> plugin =
        VST3::Hosting::Win32Module::create(path, error);

    if (plugin) {
        // TODO: They use some thin wrappers around the interfaces, we can
        //       probably reuse these instead of having to make our own
        VST3::Hosting::FactoryInfo info = plugin->getFactory().info();
        std::cout << "Plugin name:  " << plugin->getName() << std::endl;
        std::cout << "Vendor:       " << info.vendor() << std::endl;
        std::cout << "URL:          " << info.url() << std::endl;
        std::cout << "Send spam to: " << info.email() << std::endl;
    } else {
        std::cerr << "Ohnoes!" << std::endl;
        std::cerr << error << std::endl;
    }
}
