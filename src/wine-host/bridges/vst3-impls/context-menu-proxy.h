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
    ~Vst3ContextMenuProxyImpl() noexcept override;

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
     * The targets passed when to `addItem()` calls made by the plugin. This way
     * we can call these same targets later. The key here is the item's tag.
     *
     * If `getItem()` returns a context menu item with a tag that is not in this
     * map then it's from an item belonging to the host, and we'll return a
     * proxy target that would call the host's target instead.
     */
    std::unordered_map<int32,
                       Steinberg::IPtr<Steinberg::Vst::IContextMenuTarget>>
        plugin_targets_;

   private:
    Vst3Bridge& bridge_;

    /**
     * As mentioned above, these are the targets belonging to context items
     * prepopulated by the host. Because Bitwig doesn't assign a tag to its own
     * context menu items all of these this map is indexed by the **item id**.
     * Calling one of these sends a message to the host to call the
     * corresponding menu item.
     */
    std::unordered_map<int32, Steinberg::IPtr<YaContextMenuTarget>>
        host_targets_;

    /**
     * The items passed when to `addItem` calls made by the plugin. This way we
     * can call these same targets later.
     *
     * This will be initialized with targets created by the host.
     */
    std::vector<Steinberg::Vst::IContextMenuItem> items_;
};
