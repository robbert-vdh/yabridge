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

#include "boost-fix.h"

// Use the native version of xcb
#pragma push_macro("_WIN32")
#undef _WIN32
#include <xcb/xcb.h>
#pragma pop_macro("_WIN32")

#include <windows.h>
#include <boost/container/small_vector.hpp>

#include "utils.h"

/**
 * A simple, unmapped 1x1 proxy window we'll use for our Wine->X11 drag-and-drop
 * proxy so we can send and receive client messages.
 */
class ProxyWindow {
   public:
    /**
     * Create the proxy window.
     */
    ProxyWindow(std::shared_ptr<xcb_connection_t> x11_connection);

    /**
     * Destroy the window again when this object gets dropped.
     */
    ~ProxyWindow() noexcept;

    ProxyWindow(const ProxyWindow&) noexcept = delete;
    ProxyWindow& operator=(const ProxyWindow&) noexcept = delete;

    ProxyWindow(ProxyWindow&&) noexcept;
    ProxyWindow& operator=(ProxyWindow&&) noexcept;

   private:
    std::shared_ptr<xcb_connection_t> x11_connection;

   public:
    xcb_window_t window;

   private:
    bool is_moved = false;
};

/**
 * A simple wrapper that registers a WinEvents hook to listen for new windows
 * being created, and handles XDND client messages to achieve the behaviour
 * described in `WineXdndProxy::get_handle()`.
 */
class WineXdndProxy {
   protected:
    /**
     * Initialize the proxy and register all hooks.
     */
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
         * in `WineXdndProxy::get_handle()`.
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
     * tracker windows created by the Wine server. When a drag and drop
     * operation is started, we will initiate the XDND protocol with the files
     * referenced by that tracker window. This will allow us to drag files from
     * Wine windows to X11 applications, something that's normally not possible.
     * Calling this function more than once doesn't have any effect, but this
     * should still be called at least once from every plugin host instance.
     * Because the actual data is stored in a COM object, we can only handle
     * drag-and-drop coming form this process.
     *
     * This is sort of a singleton but not quite, as the `WineXdndProxy` is only
     * alive for as long as there are open editors in this process. This is done
     * to avoid opening too many X11 connections.
     *
     * @note This function, like everything other GUI realted, should be called
     *   from the main thread that's running the Win32 message loop.
     */
    static WineXdndProxy::Handle get_handle();

    /**
     * Initiate the XDDN protocol by taking ownership of the `XdndSelection`
     * selection and setting up the event listeners.
     */
    void begin_xdnd(const boost::container::small_vector_base<
                        boost::filesystem::path>& file_paths,
                    HWND tracker_window);

    /**
     * Release ownership of the selection stop listening for X11 events.
     */
    void end_xdnd();

   private:
    /**
     * From another thread, constantly poll the mouse position until the left
     * mouse button gets released, and then perform the drop if the mouse cursor
     * was last positioned over an XDND aware window. This is a workaround for
     * us not being able to grab the mouse cursor since Wine is already doing
     * that.
     */
    void run_xdnd_loop();

    /**
     * Find the first XDND aware X11 window at the current mouse cursor,
     * starting at `window` and iteratively descending into its children until
     * we reach the bottommost child where the mouse cursor is in. This respects
     * `XdndProxy`. If no XdndAware window was found, then this will contain the
     * deepest query so we still have access to the pointer coordinates. That
     * means you will still need to check `is_xdnd_aware(result->child)` after
     * the fact.
     *
     * This will return a null pointer if an X11 error was thrown.
     */
    std::unique_ptr<xcb_query_pointer_reply_t>
    query_xdnd_aware_window_at_pointer(xcb_window_t window) const noexcept;

    /**
     * Check whether a window is XDND-aware, respecting `XdndProxy`. We should
     * be checking the supported version as well and change our handling
     * accordingly, but the XDND spec was last updated in 2002 so we'll just
     * assume this won't cause any issues.
     */
    bool is_xdnd_aware(xcb_window_t window) const noexcept;

    /**
     * Return the XDND proxy window for `window` as specified in the `XdndProxy`
     * property. Returns a nullopt if `window` doesn't have that property set.
     */
    std::optional<xcb_window_t> get_xdnd_proxy(
        xcb_window_t window) const noexcept;

    /**
     * Send an XDND message to a window, respecting `XdndProxy` (i.e. window
     * should always be the window under the cursor). This does not include a
     * flush. See the spec for more information:
     *
     * https://www.freedesktop.org/wiki/Specifications/XDND/#clientmessages
     */
    void send_xdnd_message(xcb_window_t window,
                           xcb_atom_t message_type,
                           uint32_t data1,
                           uint32_t data2,
                           uint32_t data3,
                           uint32_t data4) const noexcept;

    /**
     * Handle any incoming `SelectionRequest` events.
     *
     * When the window we're dragging over wants to inspect the dragged content,
     * it will call `ConvertSelection()` which sends us a `SelelectionRequest`.
     * We should write the data in the requested format the property the
     * specified on their window, and then send them a `SelectionNotify` to
     * indicate that we're done. Since we only provide a single unique format,
     * we have already converted the file list to `text/uri-list` format.
     *
     * This does included the necessary flushes.
     */
    void handle_convert_selection(const xcb_selection_request_event_t& event);

    /**
     * We need a dedicated X11 connection for our proxy because we can have
     * multiple open editors in a single process (e.g. when using VST3 plugins
     * or plugin groups), and client messages are sent to the X11 connection
     * that created the window. So we cannot just reuse the connection from the
     * editor.
     */
    std::shared_ptr<xcb_connection_t> x11_connection;

    /**
     * We need an unmapped proxy window to send and receive client messages for
     * the XDND protocol.
     */
    ProxyWindow proxy_window;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"
    std::unique_ptr<std::remove_pointer_t<HWINEVENTHOOK>,
                    std::decay_t<decltype(&UnhookWinEvent)>>
        hook_handle;
#pragma GCC diagnostic pop

    /**
     * MT-PowerDrumkit for some reason initializes a drag-and-drop operation,
     * cancels it, and then immediately starts a new one. We need to make sure
     * that we only handle a single drag-and-drop operation at a time.
     */
    std::atomic_bool drag_active = false;

    /**
     * The files that are currently being dragged, stored as in `text/uri-list`
     * format (i.e. a list of URIs, each ending with a line feed)
     */
    std::string dragged_files_uri_list;

    /**
     * Wine's tracker window for tracking the drag-and-drop operation. When the
     * XDND operation succeeds, we make sure to close this window to avoid the
     * potential for weird race conditions where the plugin may still think
     * we're doing drag-and-drop.
     */
    HWND tracker_window;

    /**
     * We need to poll for mouse position changes from another thread, because
     * when the drag-and-drop operation starts Wine will be blocking the GUI
     * thread, so we cannot rely on the normal event loop.
     */
    Win32Thread xdnd_handler;

    /**
     * The X11 root window.
     */
    xcb_window_t root_window;

    /**
     * The X11 keycode for the escape key. We need to figure this out once when
     * the first drag-and-drop operation happens.
     */
    std::optional<xcb_keycode_t> escape_keycode;

    // These are the atoms used for the XDND protocol, as described by
    // https://www.freedesktop.org/wiki/Specifications/XDND/#atomsandproperties
    xcb_atom_t xcb_xdnd_selection;
    xcb_atom_t xcb_xdnd_aware_property;
    xcb_atom_t xcb_xdnd_proxy_property;
    xcb_atom_t xcb_xdnd_drop_message;
    xcb_atom_t xcb_xdnd_enter_message;
    xcb_atom_t xcb_xdnd_finished_message;
    xcb_atom_t xcb_xdnd_position_message;
    xcb_atom_t xcb_xdnd_status_message;
    xcb_atom_t xcb_xdnd_leave_message;

    // XDND specifies various actions for drag-and-drop, but since the file is
    // technically still owned by the plugin we'll just stick with copies to be
    // safe
    xcb_atom_t xcb_xdnd_copy_action;

    // Mime types for use in XDND, we'll only support dragging links since that
    // is the foramt the Windows OLE drag-and-drop provides us
    xcb_atom_t xcb_mime_text_uri_list;
    xcb_atom_t xcb_mime_text_plain;
};
