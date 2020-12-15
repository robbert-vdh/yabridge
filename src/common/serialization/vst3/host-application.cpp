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
    Steinberg::IPtr<Steinberg::Vst::IHostApplication> context,
    std::optional<size_t> component_instance_id)
    : component_instance_id(component_instance_id) {
    Steinberg::Vst::String128 name_array;
    if (context->getName(name_array) == Steinberg::kResultOk) {
        name = tchar_string_to_u16string(name_array);
    }
}

YaHostApplication::YaHostApplication(const ConstructArgs&& args)
    : arguments(std::move(args)){FUNKNOWN_CTOR}

      YaHostApplication::~YaHostApplication() {
    FUNKNOWN_DTOR
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
IMPLEMENT_REFCOUNT(YaHostApplication)
#pragma GCC diagnostic pop

tresult PLUGIN_API YaHostApplication::queryInterface(Steinberg::FIDString _iid,
                                                     void** obj) {
    QUERY_INTERFACE(_iid, obj, Steinberg::FUnknown::iid,
                    Steinberg::Vst::IHostApplication);
    QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IHostApplication::iid,
                    Steinberg::Vst::IHostApplication)

    *obj = nullptr;
    return Steinberg::kNoInterface;
}

tresult PLUGIN_API YaHostApplication::getName(Steinberg::Vst::String128 name) {
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
