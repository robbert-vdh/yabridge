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

// Use the native version of xcb
#pragma push_macro("_WIN32")
#undef _WIN32
#include <xcb/xcb.h>
#pragma pop_macro("_WIN32")

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
     * A sort of smart pointer for `WineXdndProxy`, similar to how the COM/VST3
     * pointers work. We want to unregister the hooks and drop the X11
     * connection when the last editor closes in a plugin group. This is not
     * strictly necessary, but there's an open X11 client limit and otherwise
     * opening and closing a bunch of editors would get you very close to that
     * limit.
     */
    class Handle {
       protected:
        /**
         * Before calling this, the reference count should be increased by one
         * in `WineXdndProxy::init_proxy()`.
         */
        Handle(WineXdndProxy* proxy);

       public:
        /**
         * Reduces the reference count by one, and frees `proxy` if this was the
         * last handle.
         */
        ~Handle() noexcept;

        Handle(const Handle&) noexcept;
        Handle& operator=(const Handle&) noexcept = default;

        Handle(Handle&&) noexcept;
        Handle& operator=(Handle&&) noexcept = default;

       private:
        WineXdndProxy* proxy;

        friend WineXdndProxy;
    };

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
     *
     * This is sort of a singleton but not quite, as the `WineXdndProxy` is only
     * alive for as long as there are open editors in this process. This is done
     * to avoid opening too many X11 connections.
     *
     * @note This function, like everything other GUI realted, should be called
     *   from the main thread that's running the Win32 message loop.
     */
    static WineXdndProxy::Handle init_proxy();

   private:
    std::unique_ptr<xcb_connection_t, decltype(&xcb_disconnect)> x11_connection;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"
    std::unique_ptr<std::remove_pointer_t<HWINEVENTHOOK>,
                    std::decay_t<decltype(&UnhookWinEvent)>>
        hook_handle;
#pragma GCC diagnostic pop
};
