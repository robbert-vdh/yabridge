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

#include "host-application.h"

YaHostApplication::ConstructArgs::ConstructArgs() {}

YaHostApplication::ConstructArgs::ConstructArgs(
    Steinberg::IPtr<Steinberg::FUnknown> object)
    : supported(false) {
    if (auto host_application =
            Steinberg::FUnknownPtr<Steinberg::Vst::IHostApplication>(object)) {
        supported = true;

        // `IHostApplication::getName`
        Steinberg::Vst::String128 name_array;
        if (host_application->getName(name_array) == Steinberg::kResultOk) {
            name = tchar_pointer_to_u16string(name_array);
        }
    }
}

YaHostApplication::YaHostApplication(const ConstructArgs&& args)
    : arguments(std::move(args)) {}

tresult PLUGIN_API YaHostApplication::getName(Steinberg::Vst::String128 name) {
    // TODO: This is now not being logged at all. It's probably better if we
    //       just drop these two functions that output cached data directly.
    //       They'll only be used once or twice anyways.
    if (arguments.name) {
        // Terminate with a null byte. There are no nice functions for copying
        // UTF-16 strings (because who would use those?).
        std::copy(arguments.name->begin(), arguments.name->end(), name);
        name[arguments.name->size()] = 0;

        return Steinberg::kResultOk;
    } else {
        return Steinberg::kNotImplemented;
    }
}
