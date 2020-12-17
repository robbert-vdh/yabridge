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

#include "plugin-proxy.h"

Vst3PluginProxy::ConstructArgs::ConstructArgs() {}

Vst3PluginProxy::ConstructArgs::ConstructArgs(
    Steinberg::IPtr<Steinberg::FUnknown> object,
    size_t instance_id)
    : instance_id(instance_id),
      audio_processor_args(object),
      component_args(object),
      edit_controller_2_args(object),
      plugin_base_args(object) {}

Vst3PluginProxy::Vst3PluginProxy(const ConstructArgs&& args)
    : YaAudioProcessor(std::move(args.audio_processor_args)),
      YaComponent(std::move(args.component_args)),
      YaEditController2(std::move(args.edit_controller_2_args)),
      YaPluginBase(std::move(args.plugin_base_args)),
      arguments(std::move(args)){FUNKNOWN_CTOR}

      Vst3PluginProxy::~Vst3PluginProxy() {
    FUNKNOWN_DTOR
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
IMPLEMENT_REFCOUNT(Vst3PluginProxy)
#pragma GCC diagnostic pop

tresult PLUGIN_API Vst3PluginProxy::queryInterface(Steinberg::FIDString _iid,
                                                   void** obj) {
    if (YaPluginBase::supported()) {
        // We had to expand the macro here because we need to cast through
        // `YaPluginBase`, since `IpluginBase` is also a base of `IComponent`
        if (Steinberg::FUnknownPrivate ::iidEqual(_iid,
                                                  Steinberg::FUnknown::iid)) {
            addRef();
            *obj = static_cast<Steinberg ::IPluginBase*>(
                static_cast<YaPluginBase*>(this));
            return ::Steinberg ::kResultOk;
        }
        if (Steinberg::FUnknownPrivate ::iidEqual(
                _iid, Steinberg::IPluginBase::iid)) {
            addRef();
            *obj = static_cast<Steinberg ::IPluginBase*>(
                static_cast<YaPluginBase*>(this));
            return ::Steinberg ::kResultOk;
        }
    }
    if (YaAudioProcessor::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IAudioProcessor::iid,
                        Steinberg::Vst::IAudioProcessor)
    }
    if (YaComponent::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IComponent::iid,
                        Steinberg::Vst::IComponent)
    }
    if (YaEditController2::supported_version_1()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IEditController::iid,
                        Steinberg::Vst::IEditController)
    }
    if (YaEditController2::supported_version_2()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IEditController2::iid,
                        Steinberg::Vst::IEditController2)
    }

    *obj = nullptr;
    return Steinberg::kNoInterface;
}
