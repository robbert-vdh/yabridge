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

class YaContextMenuTargetImpl : public YaContextMenuTarget {
   public:
    /**
     * @override
     */
    YaContextMenuTargetImpl(Vst3PluginBridge& bridge,
                            native_size_t owner_instance_id,
                            native_size_t context_menu_id,
                            int32 tag);

    /**
     * We'll override the query interface to log queries for interfaces we do
     * not (yet) support.
     */
    tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                      void** obj) override;

    // From `IContextMenuTarget`
    tresult PLUGIN_API executeMenuItem(int32 tag) override;

   private:
    Vst3PluginBridge& bridge;
};
