// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2024 Robbert van der Helm
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

#include "context-menu.h"

YaContextMenu::ConstructArgs::ConstructArgs() noexcept {}

YaContextMenu::ConstructArgs::ConstructArgs(
    Steinberg::IPtr<Steinberg::FUnknown> object) noexcept
    : supported(Steinberg::FUnknownPtr<Steinberg::Vst::IContextMenu>(object)) {
    Steinberg::FUnknownPtr<Steinberg::Vst::IContextMenu> context_menu(object);
    if (context_menu) {
        // Can't trust plugins to check for null pointers, so we'll just always
        // pass something
        Steinberg::Vst::IContextMenuTarget* dummyTarget = nullptr;

        // Prepopulate the context menu with these targets
        // NOTE: Bitwig does not actually set the tags here, so host menu items
        //       need to be identified through their item ID, not through the
        //       tag
        items.resize(context_menu->getItemCount());
        for (size_t i = 0; i < items.size(); i++) {
            context_menu->getItem(static_cast<int32>(i), items[i],
                                  &dummyTarget);
        }
    }
}

YaContextMenu::YaContextMenu(ConstructArgs&& args) noexcept
    : arguments_(std::move(args)) {}
