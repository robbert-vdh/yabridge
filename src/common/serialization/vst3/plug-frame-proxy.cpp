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

#include "plug-frame-proxy.h"

Vst3PlugFrameProxy::ConstructArgs::ConstructArgs() noexcept {}

Vst3PlugFrameProxy::ConstructArgs::ConstructArgs(
    Steinberg::IPtr<Steinberg::FUnknown> object,
    size_t owner_instance_id) noexcept
    : owner_instance_id(owner_instance_id), plug_frame_args(object) {}

Vst3PlugFrameProxy::Vst3PlugFrameProxy(ConstructArgs&& args) noexcept
    : YaPlugFrame(std::move(args.plug_frame_args)),
      arguments_(std::move(args)){FUNKNOWN_CTOR}

      Vst3PlugFrameProxy::~Vst3PlugFrameProxy() noexcept {
    FUNKNOWN_DTOR
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
IMPLEMENT_REFCOUNT(Vst3PlugFrameProxy)
#pragma GCC diagnostic pop

tresult PLUGIN_API Vst3PlugFrameProxy::queryInterface(Steinberg::FIDString _iid,
                                                      void** obj) {
    if (YaPlugFrame::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::FUnknown::iid,
                        Steinberg::IPlugFrame)
        QUERY_INTERFACE(_iid, obj, Steinberg::IPlugFrame::iid,
                        Steinberg::IPlugFrame)
    }

    *obj = nullptr;
    return Steinberg::kNoInterface;
}
