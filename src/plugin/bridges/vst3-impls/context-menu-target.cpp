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

#include "context-menu-target.h"

YaContextMenuTargetImpl::YaContextMenuTargetImpl(Vst3PluginBridge& bridge,
                                                 ConstructArgs&& args) noexcept
    : YaContextMenuTarget(std::move(args)), bridge_(bridge) {}

tresult PLUGIN_API
YaContextMenuTargetImpl::queryInterface(const Steinberg::TUID _iid,
                                        void** obj) {
    const tresult result = YaContextMenuTarget::queryInterface(_iid, obj);
    bridge_.logger_.log_query_interface(
        "In IContextMenuTarget::queryInterface()", result,
        Steinberg::FUID::fromTUID(_iid));

    return result;
}

tresult PLUGIN_API YaContextMenuTargetImpl::executeMenuItem(int32 tag) {
    return bridge_.send_message(YaContextMenuTarget::ExecuteMenuItem{
        .owner_instance_id = owner_instance_id(),
        .context_menu_id = context_menu_id(),
        .target_tag = target_tag(),
        .tag = tag});
}
