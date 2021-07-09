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

#include <memory>

#include <windows.h>

/**
 * A simple wrapper that registers a WinEvents hook to listen for new windows
 * being created, and handles XDND client messages to achieve the behaviour
 * described in `WineXdndProxy::init_proxy()`.
 */
class WineXdndProxy {
   protected:
    WineXdndProxy();

   public:
    /**
     * Initialize the Wine->X11 drag-and-drop proxy. Calling this will hook into
     * Wine's OLE drag and drop system by listening for the creation of special
     * proxy windows created by the Wine server. When a drag and drop operation
     * is started, we will initiate the XDND protocol with the same file. This
     * will allow us to drag files from Wine windows to X11 applications,
     * something that's normally not possible. Calling this function more than
     * once doesn't have any effect, but this should still be called at least
     * once from every plugin host instance. Because the actual data is stored
     * in a COM object, we can only handle drag-and-drop coming form this
     * process.
     */
    static WineXdndProxy& init_proxy();

   private:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"
    std::unique_ptr<std::remove_pointer_t<HWINEVENTHOOK>,
                    std::decay_t<decltype(&UnhookWinEvent)>>
        hook_handle;
#pragma GCC diagnostic pop
};
