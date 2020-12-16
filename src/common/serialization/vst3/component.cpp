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
    : instance_id(instance_id),
      audio_processor_args(component),
      plugin_base_args(component) {
    // `IComponent::getControllerClassId`
    Steinberg::TUID cid;
    if (component->getControllerClassId(cid) == Steinberg::kResultOk) {
        edit_controller_cid = std::to_array(cid);
    }
}

YaComponent::YaComponent(const ConstructArgs&& args)
    : YaAudioProcessor(std::move(args.audio_processor_args)),
      YaPluginBase(std::move(args.plugin_base_args)),
      arguments(std::move(args)){FUNKNOWN_CTOR}

      YaComponent::~YaComponent() {
    FUNKNOWN_DTOR
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
IMPLEMENT_REFCOUNT(YaComponent)
#pragma GCC diagnostic pop

tresult PLUGIN_API YaComponent::queryInterface(Steinberg::FIDString _iid,
                                               void** obj) {
    QUERY_INTERFACE(_iid, obj, Steinberg::FUnknown::iid,
                    Steinberg::Vst::IComponent)
    if (YaPluginBase::supported()) {
        // We had to expand the macro here because we need to cast through
        // `YaPluginBase`, since `IpluginBase` is also a base of `IComponent`
        if (Steinberg::FUnknownPrivate ::iidEqual(
                _iid, Steinberg::IPluginBase::iid)) {
            addRef();
            *obj = static_cast<Steinberg ::IPluginBase*>(
                static_cast<YaPluginBase*>(this));
            return ::Steinberg ::kResultOk;
        }
    }
    QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IComponent::iid,
                    Steinberg::Vst::IComponent)
    if (YaAudioProcessor::supported()) {
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
