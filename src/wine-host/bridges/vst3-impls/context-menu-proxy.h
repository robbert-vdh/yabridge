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

#pragma once

#include "../vst3.h"

class Vst3ContextMenuProxyImpl : public Vst3ContextMenuProxy {
   public:
    Vst3ContextMenuProxyImpl(Vst3Bridge& bridge,
                             Vst3ContextMenuProxy::ConstructArgs&& args);

    /**
     * When the reference count reaches zero and this destructor is called,
     * we'll send a request to plugin to destroy the corresponding object.
     */
    ~Vst3ContextMenuProxyImpl();

    /**
     * We'll override the query interface to log queries for interfaces we do
     * not (yet) support.
     */
    tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                      void** obj) override;

    // From `IContextMenu`
    int32 PLUGIN_API getItemCount() override;
    tresult PLUGIN_API
    getItem(int32 index,
            Steinberg::Vst::IContextMenuItem& item /*out*/,
            Steinberg::Vst::IContextMenuTarget** target /*out*/) override;
    tresult PLUGIN_API
    addItem(const Steinberg::Vst::IContextMenuItem& item,
            Steinberg::Vst::IContextMenuTarget* target) override;
    tresult PLUGIN_API
    removeItem(const Steinberg::Vst::IContextMenuItem& item,
               Steinberg::Vst::IContextMenuTarget* target) override;
    tresult PLUGIN_API popup(Steinberg::UCoord x, Steinberg::UCoord y) override;

    /**
     * The targets passed when to `addItem` calls made by the plugin. This way
     * we can call these same targets later. The key here is the item's tag.
     */
    std::map<int32, Steinberg::IPtr<Steinberg::Vst::IContextMenuTarget>>
        context_menu_targets;

   private:
    Vst3Bridge& bridge;

    /**
     * The items passed when to `addItem` calls made by the plugin. This way we
     * can call these same targets later.
     */
    std::vector<Steinberg::Vst::IContextMenuItem> items;
};
