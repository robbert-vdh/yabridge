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

#include "xdnd-proxy.h"

void CALLBACK dnd_winevent_callback(HWINEVENTHOOK hWinEventHook,
                                    DWORD event,
                                    HWND hwnd,
                                    LONG idObject,
                                    LONG idChild,
                                    DWORD idEventThread,
                                    DWORD dwmsEventTime) {
    // TODO: `EVENT_OBJECT_DESTROY` doesn't seem to be implemented by Wine, so
    //        we can't rely on that.
    if (event == EVENT_OBJECT_CREATE && idObject == OBJID_WINDOW) {
        // TODO
    }
}

WineXdndProxy::WineXdndProxy()
    : hook_handle(
          SetWinEventHook(EVENT_OBJECT_CREATE,
                          EVENT_OBJECT_CREATE,
                          nullptr,
                          dnd_winevent_callback,
                          0,
                          0,
                          WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS),
          UnhookWinEvent) {}

WineXdndProxy& WineXdndProxy::init_proxy() {
    static std::unique_ptr<WineXdndProxy> instance;
    if (!instance) {
        // Protected constructors, hooray!
        instance.reset(new WineXdndProxy{});
    }

    return *instance;
}
