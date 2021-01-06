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

#include "context-menu-proxy.h"

#include <iostream>

Vst3ContextMenuProxyImpl::Vst3ContextMenuProxyImpl(
    Vst3Bridge& bridge,
    Vst3ContextMenuProxy::ConstructArgs&& args)
    : Vst3ContextMenuProxy(std::move(args)), bridge(bridge) {}

Vst3ContextMenuProxyImpl::~Vst3ContextMenuProxyImpl() {
    // Also drop the context menu smart pointer on plugin side when this gets
    // dropped
    bridge.send_message(
        Vst3ContextMenuProxy::Destruct{.owner_instance_id = owner_instance_id(),
                                       .context_menu_id = context_menu_id()});
}

tresult PLUGIN_API
Vst3ContextMenuProxyImpl::queryInterface(const Steinberg::TUID _iid,
                                         void** obj) {
    const tresult result = Vst3ContextMenuProxy::queryInterface(_iid, obj);
    bridge.logger.log_query_interface("In IContextMenu::queryInterface()",
                                      result, Steinberg::FUID::fromTUID(_iid));

    return result;
}

int32 PLUGIN_API Vst3ContextMenuProxyImpl::getItemCount() {
    // TODO: Implement
    std::cerr << "TODO: IContextMenu::getItemCount()" << std::endl;
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API Vst3ContextMenuProxyImpl::getItem(
    int32 index,
    Steinberg::Vst::IContextMenuItem& item /*out*/,
    Steinberg::Vst::IContextMenuTarget** target /*out*/) {
    // TODO: Implement
    std::cerr << "TODO: IContextMenu::getItem()" << std::endl;
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API
Vst3ContextMenuProxyImpl::addItem(const Steinberg::Vst::IContextMenuItem& item,
                                  Steinberg::Vst::IContextMenuTarget* target) {
    // TODO: Implement
    std::cerr << "TODO: IContextMenu::addItem()" << std::endl;
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API Vst3ContextMenuProxyImpl::removeItem(
    const Item& item,
    Steinberg::Vst::IContextMenuTarget* target) {
    // TODO: Implement
    std::cerr << "TODO: IContextMenu::removeItem()" << std::endl;
    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API Vst3ContextMenuProxyImpl::popup(Steinberg::UCoord x,
                                                   Steinberg::UCoord y) {
    // TODO: Implement
    std::cerr << "TODO: IContextMenu::popup()" << std::endl;
    return Steinberg::kNotImplemented;
}
