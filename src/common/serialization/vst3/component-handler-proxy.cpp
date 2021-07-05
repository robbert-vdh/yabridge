// yabridge: a Wine VST bridge
// Copyright (C) 2020-2021 Robbert van der Helm
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

#include "component-handler-proxy.h"

Vst3ComponentHandlerProxy::ConstructArgs::ConstructArgs() noexcept {}

Vst3ComponentHandlerProxy::ConstructArgs::ConstructArgs(
    Steinberg::IPtr<Steinberg::FUnknown> object,
    size_t owner_instance_id) noexcept
    : owner_instance_id(owner_instance_id),
      component_handler_args(object),
      component_handler_2_args(object),
      component_handler_3_args(object),
      component_handler_bus_activation_args(object),
      progress_args(object),
      unit_handler_args(object),
      unit_handler_2_args(object) {}

Vst3ComponentHandlerProxy::Vst3ComponentHandlerProxy(
    ConstructArgs&& args) noexcept
    : YaComponentHandler(std::move(args.component_handler_args)),
      YaComponentHandler2(std::move(args.component_handler_2_args)),
      YaComponentHandler3(std::move(args.component_handler_3_args)),
      YaComponentHandlerBusActivation(
          std::move(args.component_handler_bus_activation_args)),
      YaProgress(std::move(args.progress_args)),
      YaUnitHandler(std::move(args.unit_handler_args)),
      YaUnitHandler2(std::move(args.unit_handler_2_args)),
      arguments(std::move(args)){FUNKNOWN_CTOR}

      Vst3ComponentHandlerProxy::~Vst3ComponentHandlerProxy() noexcept {
    FUNKNOWN_DTOR
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
IMPLEMENT_REFCOUNT(Vst3ComponentHandlerProxy)
#pragma GCC diagnostic pop

tresult PLUGIN_API
Vst3ComponentHandlerProxy::queryInterface(Steinberg::FIDString _iid,
                                          void** obj) {
    if (YaComponentHandler::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::FUnknown::iid,
                        Steinberg::Vst::IComponentHandler)
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IComponentHandler::iid,
                        Steinberg::Vst::IComponentHandler)
    }
    if (YaComponentHandler2::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IComponentHandler2::iid,
                        Steinberg::Vst::IComponentHandler2)
    }
    if (YaComponentHandler3::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IComponentHandler3::iid,
                        Steinberg::Vst::IComponentHandler3)
    }
    if (YaComponentHandlerBusActivation::supported()) {
        QUERY_INTERFACE(_iid, obj,
                        Steinberg::Vst::IComponentHandlerBusActivation::iid,
                        Steinberg::Vst::IComponentHandlerBusActivation)
    }
    if (YaProgress::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IProgress::iid,
                        Steinberg::Vst::IProgress)
    }
    if (YaUnitHandler::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IUnitHandler::iid,
                        Steinberg::Vst::IUnitHandler)
    }
    if (YaUnitHandler2::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IUnitHandler2::iid,
                        Steinberg::Vst::IUnitHandler2)
    }

    *obj = nullptr;
    return Steinberg::kNoInterface;
}
