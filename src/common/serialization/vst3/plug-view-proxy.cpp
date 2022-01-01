// yabridge: a Wine VST bridge
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

#include "plug-view-proxy.h"

Vst3PlugViewProxy::ConstructArgs::ConstructArgs() noexcept {}

Vst3PlugViewProxy::ConstructArgs::ConstructArgs(
    Steinberg::IPtr<Steinberg::FUnknown> object,
    size_t owner_instance_id) noexcept
    : owner_instance_id(owner_instance_id),
      plug_view_args(object),
      parameter_finder_args(object),
      plug_view_content_scale_support_args(object) {}

Vst3PlugViewProxy::Vst3PlugViewProxy(ConstructArgs&& args) noexcept
    : YaPlugView(std::move(args.plug_view_args)),
      YaParameterFinder(std::move(args.parameter_finder_args)),
      YaPlugViewContentScaleSupport(
          std::move(args.plug_view_content_scale_support_args)),
      arguments(std::move(args)){FUNKNOWN_CTOR}

      Vst3PlugViewProxy::~Vst3PlugViewProxy() noexcept {
    FUNKNOWN_DTOR
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
IMPLEMENT_REFCOUNT(Vst3PlugViewProxy)
#pragma GCC diagnostic pop

tresult PLUGIN_API Vst3PlugViewProxy::queryInterface(Steinberg::FIDString _iid,
                                                     void** obj) {
    if (YaPlugView::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::FUnknown::iid,
                        Steinberg::IPlugView)
        QUERY_INTERFACE(_iid, obj, Steinberg::IPlugView::iid,
                        Steinberg::IPlugView)
    }
    if (YaParameterFinder::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IParameterFinder::iid,
                        Steinberg::Vst::IParameterFinder)
    }
    if (YaPlugViewContentScaleSupport::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::IPlugViewContentScaleSupport::iid,
                        Steinberg::IPlugViewContentScaleSupport)
    }

    *obj = nullptr;
    return Steinberg::kNoInterface;
}
