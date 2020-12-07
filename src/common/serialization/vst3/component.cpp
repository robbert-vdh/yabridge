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

#include "component.h"

YaComponent::YaComponent(){FUNKNOWN_CTOR}

YaComponent::YaComponent(
    Steinberg::IPtr<Steinberg::Vst::IComponent> component) {
    FUNKNOWN_CTOR

    // `IComponent::getControllerClassId`
    component->getControllerClassId(edit_controller_cid);

    // Everything else is handled directly through callbacks to minimize the
    // potential for errors
}

YaComponent::~YaComponent() {
    FUNKNOWN_DTOR
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
IMPLEMENT_REFCOUNT(YaComponent)
#pragma GCC diagnostic pop

tresult PLUGIN_API YaComponent::queryInterface(Steinberg::FIDString _iid,
                                               void** obj) {
    QUERY_INTERFACE(_iid, obj, Steinberg::FUnknown::iid, Steinberg::IPluginBase)
    QUERY_INTERFACE(_iid, obj, Steinberg::IPluginBase::iid,
                    Steinberg::IPluginBase)
    QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IComponent::iid,
                    Steinberg::Vst::IComponent)

    *obj = nullptr;
    return Steinberg::kNoInterface;
}
