// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2023 Robbert van der Helm
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

#include "host-context-proxy.h"

Vst3HostContextProxy::ConstructArgs::ConstructArgs() noexcept {}

Vst3HostContextProxy::ConstructArgs::ConstructArgs(
    Steinberg::IPtr<Steinberg::FUnknown> object,
    std::optional<size_t> owner_instance_id) noexcept
    : owner_instance_id(owner_instance_id),
      host_application_args(object),
      plug_interface_support_args(object) {}

Vst3HostContextProxy::Vst3HostContextProxy(ConstructArgs&& args) noexcept
    : YaHostApplication(std::move(args.host_application_args)),
      YaPlugInterfaceSupport(std::move(args.plug_interface_support_args)),
      arguments_(std::move(args)){FUNKNOWN_CTOR}

      Vst3HostContextProxy::~Vst3HostContextProxy() noexcept {FUNKNOWN_DTOR}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
      IMPLEMENT_REFCOUNT(Vst3HostContextProxy)
#pragma GCC diagnostic pop

          tresult PLUGIN_API Vst3HostContextProxy::queryInterface(
              Steinberg::FIDString _iid,
              void** obj) {
    if (YaHostApplication::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::FUnknown::iid,
                        Steinberg::Vst::IHostApplication)
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IHostApplication::iid,
                        Steinberg::Vst::IHostApplication)
    }
    if (YaPlugInterfaceSupport::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IPlugInterfaceSupport::iid,
                        Steinberg::Vst::IPlugInterfaceSupport)
    }

    *obj = nullptr;
    return Steinberg::kNoInterface;
}
