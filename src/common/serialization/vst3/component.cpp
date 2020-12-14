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

YaComponent::ConstructArgs::ConstructArgs() {}

YaComponent::ConstructArgs::ConstructArgs(
    Steinberg::IPtr<Steinberg::Vst::IComponent> component,
    size_t instance_id)
    : instance_id(instance_id) {
    known_iids.insert(component->iid);
    // `IComponent::getControllerClassId`
    Steinberg::TUID cid;
    if (component->getControllerClassId(cid) == Steinberg::kResultOk) {
        edit_controller_cid = std::to_array(cid);
    }

    // There's no static data we can copy from the audio processor
    if (auto audio_processor =
            Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor>(
                component)) {
        known_iids.insert(Steinberg::Vst::IAudioProcessor::iid);
    }
}

YaComponent::YaComponent(const ConstructArgs&& args) : arguments(std::move(args)) {
    FUNKNOWN_CTOR

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
    if (arguments.known_iids.contains(Steinberg::Vst::IComponent::iid)) {
        QUERY_INTERFACE(_iid, obj, Steinberg::IPluginBase::iid,
                        Steinberg::IPluginBase)
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IComponent::iid,
                        Steinberg::Vst::IComponent)
    }
    if (arguments.known_iids.contains(Steinberg::Vst::IAudioProcessor::iid)) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IAudioProcessor::iid,
                        Steinberg::Vst::IAudioProcessor)
    }

    *obj = nullptr;
    return Steinberg::kNoInterface;
}

tresult PLUGIN_API YaComponent::getControllerClassId(Steinberg::TUID classId) {
    if (arguments.edit_controller_cid) {
        std::copy(arguments.edit_controller_cid->begin(),
                  arguments.edit_controller_cid->end(), classId);
        return Steinberg::kResultOk;
    } else {
        return Steinberg::kNotImplemented;
    }
}
