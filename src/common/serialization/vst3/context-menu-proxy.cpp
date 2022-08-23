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

#include "context-menu-proxy.h"

Vst3ContextMenuProxy::ConstructArgs::ConstructArgs() noexcept {}

Vst3ContextMenuProxy::ConstructArgs::ConstructArgs(
    Steinberg::IPtr<Steinberg::FUnknown> object,
    size_t owner_instance_id,
    size_t context_menu_id)
    : owner_instance_id(owner_instance_id),
      context_menu_id(context_menu_id),
      context_menu_args(object) {}

Vst3ContextMenuProxy::Vst3ContextMenuProxy(ConstructArgs&& args) noexcept
    : YaContextMenu(std::move(args.context_menu_args)),
      arguments_(std::move(args)){FUNKNOWN_CTOR}

      Vst3ContextMenuProxy::~Vst3ContextMenuProxy() noexcept {FUNKNOWN_DTOR}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
      IMPLEMENT_REFCOUNT(Vst3ContextMenuProxy)
#pragma GCC diagnostic pop

          tresult PLUGIN_API Vst3ContextMenuProxy::queryInterface(
              Steinberg::FIDString _iid,
              void** obj) {
    if (YaContextMenu::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::FUnknown::iid,
                        Steinberg::Vst::IContextMenu)
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IContextMenu::iid,
                        Steinberg::Vst::IContextMenu)
    }

    *obj = nullptr;
    return Steinberg::kNoInterface;
}
