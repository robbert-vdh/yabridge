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

#include <iostream>
#include <numeric>

#include "editor.h"

using namespace std::literals::chrono_literals;

namespace fs = boost::filesystem;

/**
 * The window class name Wine uses for its `DoDragDrop()` tracker window.
 *
 * https://github.com/wine-mirror/wine/blob/d10887b8f56792ebcca717ccc28a289f7bcaf107/dlls/ole32/ole2.c#L101-L104
 */
constexpr char OLEDD_DRAGTRACKERCLASS[] = "WineDragDropTracker32";

// These are the XDND atom names as described in
// https://www.freedesktop.org/wiki/Specifications/XDND/#atomsandproperties
constexpr char xdnd_selection_name[] = "XdndSelection";
// xdnd_aware_property_name is defined in `editor.h``
constexpr char xdnd_proxy_property_name[] = "XdndProxy";
constexpr char xdnd_drop_message_name[] = "XdndDrop";
constexpr char xdnd_enter_message_name[] = "XdndEnter";
constexpr char xdnd_finished_message_name[] = "XdndFinished";
constexpr char xdnd_position_message_name[] = "XdndPosition";
constexpr char xdnd_status_message_name[] = "XdndStatus";
constexpr char xdnd_leave_message_name[] = "XdndLeave";

// XDND actions
constexpr char xdnd_copy_action_name[] = "XdndActionCopy";

// Mime types for use in XDND
constexpr char mime_text_uri_list_name[] = "text/uri-list";
constexpr char mime_text_plain_name[] = "text/plain";

// We can cheat by just using the Win32 cursors instead of providing our own
static const HCURSOR dnd_accepted_cursor = LoadCursor(nullptr, IDC_HAND);
static const HCURSOR dnd_denied_cursor = LoadCursor(nullptr, IDC_NO);

/**
 * We're doing a bit of a hybrid between a COM-style reference counted smart
 * pointer and a singleton here because we need to ensure that there's only one
 * proxy per process, but we want to free up the X11 connection when it's not
 * needed anymore. Because of that this pointer may point to deallocated memory,
 * so the reference count should be leading here. Oh and explained elsewhere, we
 * won't even bother making this thread safe because it can only be called from
 * the GUI thread anyways.
 */
static WineXdndProxy* instance = nullptr;

/**
 * The number of handles to our Wine->X11 drag-and-drop proxy object. To prevent
 * running out of X11 connections when opening and closing a lot of plugin
 * editors in a project, we'll free this again after the last editor in this
 * process gets closed.
 */
static std::atomic_size_t instance_reference_count = 0;

void CALLBACK dnd_winevent_callback(HWINEVENTHOOK hWinEventHook,
                                    DWORD event,
                                    HWND hwnd,
                                    LONG idObject,
                                    LONG idChild,
                                    DWORD idEventThread,
                                    DWORD dwmsEventTime);

ProxyWindow::ProxyWindow(std::shared_ptr<xcb_connection_t> x11_connection)
    : x11_connection(x11_connection),
      window(xcb_generate_id(x11_connection.get())) {
    const xcb_screen_t* screen =
        xcb_setup_roots_iterator(xcb_get_setup(x11_connection.get())).data;

    xcb_create_window(x11_connection.get(), XCB_COPY_FROM_PARENT, window,
                      screen->root, 0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY,
                      XCB_COPY_FROM_PARENT, 0, nullptr);
    xcb_flush(x11_connection.get());
}

ProxyWindow::~ProxyWindow() noexcept {
    if (!is_moved) {
        xcb_destroy_window(x11_connection.get(), window);
        xcb_flush(x11_connection.get());
    }
}

ProxyWindow::ProxyWindow(ProxyWindow&& o) noexcept
    : x11_connection(std::move(o.x11_connection)), window(std::move(o.window)) {
    o.is_moved = true;
}
ProxyWindow& ProxyWindow::operator=(ProxyWindow&& o) noexcept {
    if (&o != this) {
        x11_connection = std::move(o.x11_connection);
        window = std::move(o.window);

        o.is_moved = true;
    }

    return *this;
}

WineXdndProxy::WineXdndProxy()
    : x11_connection(xcb_connect(nullptr, nullptr), xcb_disconnect),
      proxy_window(x11_connection),
      hook_handle(
          SetWinEventHook(EVENT_OBJECT_CREATE,
                          EVENT_OBJECT_CREATE,
                          nullptr,
                          dnd_winevent_callback,
                          0,
                          0,
                          WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS),
          UnhookWinEvent) {
    // XDND uses a whole load of atoms for its messages, properties, and
    // selections
    xcb_xdnd_selection = get_atom_by_name(*x11_connection, xdnd_selection_name);
    xcb_xdnd_aware_property =
        get_atom_by_name(*x11_connection, xdnd_aware_property_name);
    xcb_xdnd_proxy_property =
        get_atom_by_name(*x11_connection, xdnd_proxy_property_name);
    xcb_xdnd_drop_message =
        get_atom_by_name(*x11_connection, xdnd_drop_message_name);
    xcb_xdnd_enter_message =
        get_atom_by_name(*x11_connection, xdnd_enter_message_name);
    xcb_xdnd_finished_message =
        get_atom_by_name(*x11_connection, xdnd_finished_message_name);
    xcb_xdnd_position_message =
        get_atom_by_name(*x11_connection, xdnd_position_message_name);
    xcb_xdnd_status_message =
        get_atom_by_name(*x11_connection, xdnd_status_message_name);
    xcb_xdnd_leave_message =
        get_atom_by_name(*x11_connection, xdnd_leave_message_name);

    xcb_xdnd_copy_action =
        get_atom_by_name(*x11_connection, xdnd_copy_action_name);

    xcb_mime_text_uri_list =
        get_atom_by_name(*x11_connection, mime_text_uri_list_name);
    xcb_mime_text_plain =
        get_atom_by_name(*x11_connection, mime_text_plain_name);
}

WineXdndProxy::Handle::Handle(WineXdndProxy* proxy) : proxy(proxy) {}

WineXdndProxy::Handle::~Handle() noexcept {
    if (instance_reference_count.fetch_sub(1) == 1) {
        delete proxy;
    }
}

WineXdndProxy::Handle::Handle(const Handle& o) noexcept : proxy(o.proxy) {
    instance_reference_count += 1;
}

WineXdndProxy::Handle::Handle(Handle&& o) noexcept : proxy(o.proxy) {
    instance_reference_count += 1;
}

WineXdndProxy::Handle WineXdndProxy::get_handle() {
    // See the `instance` global above for an explanation on what's going on
    // here.
    if (instance_reference_count.fetch_add(1) == 0) {
        instance = new WineXdndProxy{};
    }

    return Handle(instance);
}

void WineXdndProxy::begin_xdnd(const boost::container::small_vector_base<
                                   boost::filesystem::path>& file_paths,
                               HWND tracker_window) {
    if (file_paths.empty()) {
        throw std::runtime_error("Cannot drag-and-drop without any files");
    }

    // NOTE: Needed for a quirk in MT-PowerDrumkit
    bool expected = false;
    if (!drag_active.compare_exchange_strong(expected, true)) {
        throw std::runtime_error("A drag-and-drop operation is already active");
    }

    // When XDND starts, we need to start listening for mouse events so we can
    // react when the mouse cursor hovers over a target that supports XDND. The
    // actual file contents will be transferred over X11 selections. See the
    // spec for a description of the entire process:
    // https://www.freedesktop.org/wiki/Specifications/XDND/#atomsandproperties
    xcb_set_selection_owner(x11_connection.get(), proxy_window.window,
                            xcb_xdnd_selection, XCB_CURRENT_TIME);
    xcb_flush(x11_connection.get());

    // We will transfer the files in `text/uri-list` format, so a string of URIs
    // separated by line feeds. When the target window requests the selection to
    // be converted, they will ask us to write this to a property on their
    // window
    constexpr char file_protocol[] = "file://";
    dragged_files_uri_list.clear();
    dragged_files_uri_list.reserve(std::accumulate(
        file_paths.begin(), file_paths.end(), 0,
        [](size_t size, const auto& path) {
            // Account for the protocol, the trailing line feed, and URL
            // encoding
            return size +
                   static_cast<size_t>(static_cast<double>(path.size()) * 1.2);
        }));
    for (const auto& path : file_paths) {
        dragged_files_uri_list.append(file_protocol);
        dragged_files_uri_list.append(url_encode_path(path.string()));
        dragged_files_uri_list.push_back('\n');
    }

    // Normally at this point you would grab the mouse pointer and track what
    // windows it's moving over. Wine is already doing this, so as a hacky
    // workaround we will just poll the mouse position every millisecond until
    // the left mouse button gets released. Because Wine is also blocking the
    // GUI thread, we need to do our XDND polling from another thread. Luckily
    // the X11 API is thread safe.
    this->tracker_window = tracker_window;
    xdnd_handler = Win32Thread([&]() { run_xdnd_loop(); });
}

void WineXdndProxy::end_xdnd() {
    xcb_set_selection_owner(x11_connection.get(), XCB_NONE, xcb_xdnd_selection,
                            XCB_CURRENT_TIME);
    xcb_flush(x11_connection.get());

    drag_active = false;
}

// FIXME: For some reason you get a -Wmaybe-uninitialized false positive with
//        GCC 11.1.0 if you just dereference `last_window` here:
//        https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80635
//
//        Oh and Clang doesn't know about -Wmaybe-uninitialized, so we need to
//        ignore some more warnings here to get clangd to not complain
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wunknown-warning-option"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"

void WineXdndProxy::run_xdnd_loop() {
    const xcb_window_t root_window =
        xcb_setup_roots_iterator(xcb_get_setup(x11_connection.get()))
            .data->root;
    const HWND windows_desktop_window = GetDesktopWindow();

    std::optional<xcb_window_t> last_xdnd_window;
    // Position and status messages should be sent in lockstep, which makes
    // everything a bit more complicated. Because of that we may need to spool
    // the `XdndPosition` messages. This field stores the next position we
    // should send to `last_xdnd_window` (i.e. `(root_x << 16) | root_y`). We
    // won't need to spool anything when `last_window_accepted_status` contains
    // a value.
    std::optional<uint32_t> next_position_message_position;
    // We need to wait until we receive the last `XdndStatus` message until we
    // send a leave, finished, or another position message
    bool last_window_accepted_status = false;
    bool waiting_for_status_message = false;

    auto maybe_leave_last_window = [&]() {
        if (last_xdnd_window) {
            send_xdnd_message(*last_xdnd_window, xcb_xdnd_leave_message, 0, 0,
                              0, 0);
            next_position_message_position.reset();
            last_window_accepted_status = false;
            waiting_for_status_message = false;

            xcb_flush(x11_connection.get());
        }
    };

    auto maybe_send_spooled_status_message = [&]() {
        if (next_position_message_position && !waiting_for_status_message) {
            send_xdnd_message(*last_xdnd_window, xcb_xdnd_position_message, 0,
                              *next_position_message_position, XCB_CURRENT_TIME,
                              xcb_xdnd_copy_action);
            next_position_message_position.reset();
            waiting_for_status_message = true;

            xcb_flush(x11_connection.get());
        }
    };

    auto handle_xdnd_status_message =
        [&](const xcb_client_message_event_t& event) {
            const bool accepts_drop =
                static_cast<bool>(event.data.data32[1] & 0b01);

            // Because this is a Winelib we can cheat a bit here so
            // we don't have to create our own cursors. This will
            // probably also look better anyways.
            // XXX: Because Wine is also changing the cursor to a
            //      denied symbol at the same time this looks a bit
            //      off. Would it be better to just not do anything
            //      at all here?
            if (accepts_drop) {
                SetCursor(dnd_accepted_cursor);
            } else {
                SetCursor(dnd_denied_cursor);
            }

            last_window_accepted_status = accepts_drop;
            waiting_for_status_message = false;
        };

    // We cannot just grab the pointer because Wine is already doing that, and
    // it's also blocking the GUI thread. So instead we will periodically poll
    // the mouse cursor position, and we will end the drag once the left mouse
    // button gets released.
    bool left_mouse_button_held = true;
    std::optional<uint16_t> last_pointer_x;
    std::optional<uint16_t> last_pointer_y;
    while (left_mouse_button_held) {
        usleep(1000);

        std::unique_ptr<xcb_generic_event_t> generic_event;
        while (generic_event.reset(xcb_poll_for_event(x11_connection.get())),
               generic_event != nullptr) {
            const uint8_t event_type =
                generic_event->response_type & xcb_event_type_mask;
            switch (event_type) {
                case XCB_SELECTION_REQUEST: {
                    handle_convert_selection(
                        *reinterpret_cast<xcb_selection_request_event_t*>(
                            generic_event.get()));
                } break;
                case XCB_CLIENT_MESSAGE: {
                    const auto event =
                        reinterpret_cast<xcb_client_message_event_t*>(
                            generic_event.get());

                    if (event->type == xcb_xdnd_status_message) {
                        handle_xdnd_status_message(*event);
                    }
                } break;
            }
        }

        // As explained above, we may need to spool these position messages
        // because they can only be sent again after we receive an `XdndStatus`
        // reply
        maybe_send_spooled_status_message();

        // We'll try to find the first window under the pointer (starting form
        // the root) until we find a window that supports XDND. The returned
        // child window may not support XDND so we need to check that
        // separately, as we still need to keep track of the pointer
        // coordinates.
        const std::unique_ptr<xcb_query_pointer_reply_t> xdnd_window_query =
            query_xdnd_aware_window_at_pointer(root_window);
        if (!xdnd_window_query) {
            continue;
        }

        // We will stop the dragging operation as soon as the left mouse button
        // gets released
        // NOTE: In soem cases Wine's own drag-and-drop operation ends
        //       prematurely. This seems to often happen with JUCE plugins. We
        //       will still continue with the dragging operation, although at
        //       that point the mouse pointer isn't grabbed by anything anymore.
        left_mouse_button_held = xdnd_window_query->mask & XCB_BUTTON_MASK_1;
        if (xdnd_window_query->root_x == last_pointer_x &&
            xdnd_window_query->root_y == last_pointer_y) {
            continue;
        }

        last_pointer_x = xdnd_window_query->root_x;
        last_pointer_y = xdnd_window_query->root_y;
        if (!is_xdnd_aware(xdnd_window_query->child)) {
            maybe_leave_last_window();
            last_xdnd_window.reset();
            continue;
        }

        // We want to ignore all Wine windows (within this prefix), since Wine
        // will be able to handle the drag-and-drop better than we can
        POINT windows_pointer_pos;
        GetCursorPos(&windows_pointer_pos);
        if (HWND windows_window = WindowFromPoint(windows_pointer_pos);
            windows_window && windows_window != windows_desktop_window) {
            // NOTE: Because resizing reparented Wine windows without XEmbed is
            //       a bit janky, yabridge creates windows with client areas
            //       large enough to fit the entire screen, and the plugin then
            //       embeds its own GUI in a portion of that. The result is that
            //       `WindowFromPoint()` will still return that same huge window
            //       when we hover over an area to the right or to the bottom of
            //       a plugin GUI. We can easily detect and skip that though,
            //       since the embedded plugin windows won't have an  X11 window
            //       ID property.
            const xcb_window_t x11_window =
                static_cast<xcb_window_t>(reinterpret_cast<size_t>(
                    GetProp(windows_window, "__wine_x11_whole_window")));
            if (x11_window == XCB_NONE) {
                maybe_leave_last_window();
                last_xdnd_window.reset();
                continue;
            }
        }

        // When transitioning between windows we need to announce this to
        // both windows
        if (last_xdnd_window != xdnd_window_query->child) {
            maybe_leave_last_window();

            // We need to announce which file formats we support. There are a
            // couple more common ones, but with `text/uri-list` and
            // `text/plain` we should cover most applications, and this is also
            // the recommended format for links/paths elsewhere:
            // https://developer.mozilla.org/en-US/docs/Web/API/HTML_Drag_and_Drop_API/Recommended_drag_types#link
            send_xdnd_message(xdnd_window_query->child, xcb_xdnd_enter_message,
                              5 << 24, xcb_mime_text_uri_list,
                              xcb_mime_text_plain, XCB_NONE);
        }

        // When the pointer is being moved inside of a window, we should
        // continuously send `XdndPosition` messages to that window. If the
        // window has not yet sent an `XdndStatus` reply to our last
        // `XdndPosition` message, then we need to spool this message and try
        // again on the next iteration.
        if (last_xdnd_window) {
            // XXX: We'll always stick with the copy action for now because that
            //      seems safer than allowing the host to move the file
            const uint32_t position =
                (xdnd_window_query->root_x << 16) | xdnd_window_query->root_y;
            if (!waiting_for_status_message) {
                send_xdnd_message(xdnd_window_query->child,
                                  xcb_xdnd_position_message, 0, position,
                                  XCB_CURRENT_TIME, xcb_xdnd_copy_action);
                waiting_for_status_message = true;
            } else {
                next_position_message_position = position;
            }
        }

        // For efficiency's sake we'll only flush all of the client messages
        // we're sending once at the end of every cycle
        xcb_flush(x11_connection.get());

        last_xdnd_window = xdnd_window_query->child;
    }

    // After the loop has finished we either:
    // 1) Finish the drop, if `last_xdnd_window` is a valid XDND window
    // 2) Cancel the drop, if the escape key is being held, or
    // 3) Don't do antyhing, if `last_xdnd_window` is a nullopt
    // TODO: Check if the escape key is pressed to allow cancelling the drop. In
    //       that case we should call `maybe_leave_last_window()`
    if (!last_xdnd_window) {
        end_xdnd();
        return;
    }

    // After the left mouse button has been released we will try to send the
    // drop to the last window we hovered over, if it was a valid XDND aware
    // window. We should however wait with this until the window has accepted
    // our `XdndPosition` message with an `XdndStatus`
    bool drop_finished = false;
    const std::chrono::steady_clock::time_point wait_start =
        std::chrono::steady_clock::now();
    while (!drop_finished) {
        // In case that window somehow becomes unresponsive or disappears, we
        // will set a timeout here to avoid hanging
        if (std::chrono::steady_clock::now() - wait_start > 5s) {
            // Just to make it extra clear that we don't want to interfere with
            // Wine's own drag-and-drop if we reach a timeout
            drop_finished = false;
            maybe_leave_last_window();
            break;
        }

        usleep(1000);

        std::unique_ptr<xcb_generic_event_t> generic_event;
        while (generic_event.reset(xcb_poll_for_event(x11_connection.get())),
               generic_event != nullptr) {
            const uint8_t event_type =
                generic_event->response_type & xcb_event_type_mask;
            switch (event_type) {
                case XCB_SELECTION_REQUEST: {
                    handle_convert_selection(
                        *reinterpret_cast<xcb_selection_request_event_t*>(
                            generic_event.get()));
                } break;
                case XCB_CLIENT_MESSAGE: {
                    const auto event =
                        reinterpret_cast<xcb_client_message_event_t*>(
                            generic_event.get());

                    if (event->type == xcb_xdnd_status_message) {
                        // We may have to wait for the last `XdndStatus` to be
                        // sent by the target window
                        handle_xdnd_status_message(*event);
                    } else if (event->type == xcb_xdnd_finished_message) {
                        // At this point we're done here, and we can clean up
                        // and terminate this thread
                        drop_finished = true;
                    }
                } break;
            }
        }

        // We May very well still have one unsent position change left
        maybe_send_spooled_status_message();

        // After we receive the last `XdndStatus` message we'll know whether the
        // window accepts or denies the drop
        if (!waiting_for_status_message) {
            if (last_window_accepted_status) {
                send_xdnd_message(*last_xdnd_window, xcb_xdnd_drop_message, 0,
                                  XCB_CURRENT_TIME, 0, 0);
            } else {
                maybe_leave_last_window();
                drop_finished = true;
            }

            xcb_flush(x11_connection.get());

            // We obviously don't want to spam the other client
            waiting_for_status_message = true;
        }
    }

    // Make sure the Windows drag-and-drop operation doesn't get stuck for
    // whatever reason (it shouldn't but who knows)
    if (drop_finished) {
        PostMessageW(tracker_window, WM_KEYDOWN, VK_ESCAPE, 0);
    }

    end_xdnd();
}

#pragma GCC diagnostic pop

std::unique_ptr<xcb_query_pointer_reply_t>
WineXdndProxy::query_xdnd_aware_window_at_pointer(
    xcb_window_t window) const noexcept {
    xcb_generic_error_t* error = nullptr;
    xcb_query_pointer_cookie_t query_pointer_cookie;
    std::unique_ptr<xcb_query_pointer_reply_t> query_pointer_reply = nullptr;
    while (true) {
        query_pointer_cookie = xcb_query_pointer(x11_connection.get(), window);
        query_pointer_reply.reset(xcb_query_pointer_reply(
            x11_connection.get(), query_pointer_cookie, &error));
        if (error) {
            free(error);
            break;
        }

        // We want to find the first XDND aware window under the mouse pointer,
        // if there is any
        if (query_pointer_reply->child == XCB_NONE ||
            is_xdnd_aware(query_pointer_reply->child)) {
            break;
        }

        window = query_pointer_reply->child;
    }

    return query_pointer_reply;
}

bool WineXdndProxy::is_xdnd_aware(xcb_window_t window) const noexcept {
    // Respect `XdndProxy`, if that's set
    window = get_xdnd_proxy(window).value_or(window);

    xcb_generic_error_t* error = nullptr;
    const xcb_get_property_cookie_t property_cookie =
        xcb_get_property(x11_connection.get(), false, window,
                         xcb_xdnd_aware_property, XCB_ATOM_ATOM, 0, 1);
    const std::unique_ptr<xcb_get_property_reply_t> property_reply(
        xcb_get_property_reply(x11_connection.get(), property_cookie, &error));
    if (error) {
        free(error);
        return false;
    }

    // Since the spec dates from 2002, we won't even bother checking the
    // supported version
    return property_reply->type != XCB_NONE &&
           *static_cast<xcb_atom_t*>(
               xcb_get_property_value(property_reply.get())) != 0;
}

std::optional<xcb_window_t> WineXdndProxy::get_xdnd_proxy(
    xcb_window_t window) const noexcept {
    xcb_generic_error_t* error = nullptr;
    const xcb_get_property_cookie_t property_cookie =
        xcb_get_property(x11_connection.get(), false, window,
                         xcb_xdnd_proxy_property, XCB_ATOM_WINDOW, 0, 1);
    const std::unique_ptr<xcb_get_property_reply_t> property_reply(
        xcb_get_property_reply(x11_connection.get(), property_cookie, &error));
    if (error) {
        free(error);
        return std::nullopt;
    }

    if (property_reply->type == XCB_NONE) {
        return std::nullopt;
    } else {
        return *static_cast<xcb_window_t*>(
            xcb_get_property_value(property_reply.get()));
    }
}

// FIXME: See above for more context, spurious warning is generated by passing
//        `*last_xdnd_window` to this function
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wunknown-warning-option"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void WineXdndProxy::send_xdnd_message(xcb_window_t window,
                                      xcb_atom_t message_type,
                                      uint32_t data1,
                                      uint32_t data2,
                                      uint32_t data3,
                                      uint32_t data4) const noexcept {
    // See https://www.freedesktop.org/wiki/Specifications/XDND/#clientmessages
    xcb_client_message_event_t event{};
    event.response_type = XCB_CLIENT_MESSAGE;
    event.type = message_type;
    // If `window` has `XdndProxy` set, then we should still mention that window
    // here, even though we will send the message to another window.
    event.window = window;
    event.format = 32;
    // THis is the source window, so the other side cna reply
    event.data.data32[0] = proxy_window.window;
    event.data.data32[1] = data1;
    event.data.data32[2] = data2;
    event.data.data32[3] = data3;
    event.data.data32[4] = data4;

    // Make sure to respect `XdndProxy` only here, as explaiend in the spec
    xcb_send_event(x11_connection.get(), false,
                   get_xdnd_proxy(window).value_or(window),
                   XCB_EVENT_MASK_NO_EVENT, reinterpret_cast<char*>(&event));
}

#pragma GCC diagnostic pop

void WineXdndProxy::handle_convert_selection(
    const xcb_selection_request_event_t& event) {
    xcb_change_property(x11_connection.get(), XCB_PROP_MODE_REPLACE,
                        event.requestor, event.property, event.target, 8,
                        dragged_files_uri_list.size(),
                        dragged_files_uri_list.c_str());
    xcb_flush(x11_connection.get());

    xcb_selection_notify_event_t selection_notify_event{};
    selection_notify_event.response_type = XCB_SELECTION_NOTIFY;
    selection_notify_event.requestor = event.requestor;
    selection_notify_event.selection = xcb_xdnd_selection;
    selection_notify_event.target = event.target;
    selection_notify_event.property = event.property;

    xcb_send_event(x11_connection.get(), false, event.requestor, XCB_NONE,
                   reinterpret_cast<const char*>(&selection_notify_event));
    xcb_flush(x11_connection.get());
}

/**
 * Part of the struct Wine uses to keep track of the data during an OLE
 * drag-and-drop operation. We only really care about the first field that
 * contains the actual data.
 *
 * https://github.com/wine-mirror/wine/blob/d10887b8f56792ebcca717ccc28a289f7bcaf107/dlls/ole32/ole2.c#L54-L73
 */
struct TrackerWindowInfo {
    IDataObject* dataObject;
    IDropSource* dropSource;
    // ... more fields that we don't need
};

void CALLBACK dnd_winevent_callback(HWINEVENTHOOK /*hWinEventHook*/,
                                    DWORD event,
                                    HWND hwnd,
                                    LONG idObject,
                                    LONG /*idChild*/,
                                    DWORD /*idEventThread*/,
                                    DWORD /*dwmsEventTime*/) {
    if (!(event == EVENT_OBJECT_CREATE && idObject == OBJID_WINDOW)) {
        return;
    }

    // Don't handle windows that weren't created in this process, because
    // otherwise we obviously cannot access the `IDataObject` object
    uint32_t process_id = 0;
    GetWindowThreadProcessId(hwnd, &process_id);
    if (process_id != GetCurrentProcessId()) {
        return;
    }

    // Wine's drag-and-drop tracker windows always have the same window
    // class name, so we can easily identify them
    {
        std::array<char, 64> window_class_name{0};
        GetClassName(hwnd, window_class_name.data(), window_class_name.size());
        if (strcmp(window_class_name.data(), OLEDD_DRAGTRACKERCLASS) != 0) {
            return;
        }
    }

    // They apaprently use 0 instead of `GWLP_USERDATA` to store the tracker
    // data
    auto tracker_info =
        reinterpret_cast<TrackerWindowInfo*>(GetWindowLongPtr(hwnd, 0));
    if (!tracker_info || !tracker_info->dataObject) {
        return;
    }

    IEnumFORMATETC* enumerator = nullptr;
    tracker_info->dataObject->EnumFormatEtc(DATADIR_GET, &enumerator);
    if (!enumerator) {
        return;
    }

    // The plugin will indicate which formats they support for the
    // drag-and-drop. In practice this is always going to be a single `HDROP`
    // (through some `HGLOBAL` global memory) that contains a single file path.
    // With this information we will set up XDND with those file paths, so we
    // can drop the files onto native applications.
    std::array<FORMATETC, 16> supported_formats{};
    unsigned int num_formats = 0;
    enumerator->Next(supported_formats.size(), supported_formats.data(),
                     &num_formats);
    enumerator->Release();

    // NOTE: MeldaProduction plugins don't return any supported formats for some
    //       reason, so we'll hardcode a HDROP
    if (num_formats == 0) {
        std::cerr << "WARNING: The plugin didn't specify any formats for the"
                  << std::endl;
        std::cerr << "         drag-and-drop operation, trying an HDROP"
                  << std::endl;

        supported_formats[0].cfFormat = CF_HDROP;
        supported_formats[0].ptd = nullptr;
        supported_formats[0].dwAspect = -1;
        supported_formats[0].lindex = 0;
        supported_formats[0].tymed = TYMED_HGLOBAL;
        num_formats = 1;
    }

    // This will contain the normal, Unix-style paths to the files
    boost::container::small_vector<fs::path, 4> dragged_files;
    for (unsigned int format_idx = 0; format_idx < num_formats; format_idx++) {
        STGMEDIUM storage{};
        if (HRESULT result = tracker_info->dataObject->GetData(
                &supported_formats[format_idx], &storage);
            result == S_OK) {
            switch (storage.tymed) {
                case TYMED_HGLOBAL: {
                    auto drop = static_cast<HDROP>(GlobalLock(storage.hGlobal));
                    if (!drop) {
                        std::cerr << "Failed to lock global memory in "
                                     "drag-and-drop operation"
                                  << std::endl;
                        continue;
                    }

                    std::array<WCHAR, 1024> file_name{0};
                    const uint32_t num_files = DragQueryFileW(
                        drop, 0xFFFFFFFF, file_name.data(), file_name.size());
                    for (uint32_t file_idx = 0; file_idx < num_files;
                         file_idx++) {
                        file_name[0] = 0;
                        DragQueryFileW(drop, file_idx, file_name.data(),
                                       file_name.size());

                        // Normalize the paths to something a bit more friendly
                        const char* unix_path =
                            wine_get_unix_file_name(file_name.data());
                        if (unix_path) {
                            boost::system::error_code err;
                            const fs::path cannonical_path =
                                boost::filesystem::canonical(unix_path, err);
                            if (err) {
                                dragged_files.emplace_back(unix_path);
                            } else {
                                dragged_files.emplace_back(cannonical_path);
                            }
                        }
                    }

                    GlobalUnlock(storage.hGlobal);
                } break;
                case TYMED_FILE: {
                    const char* unix_path =
                        wine_get_unix_file_name(storage.lpszFileName);
                    if (unix_path) {
                        boost::system::error_code err;
                        const fs::path cannonical_path =
                            boost::filesystem::canonical(unix_path, err);
                        if (err) {
                            dragged_files.emplace_back(unix_path);
                        } else {
                            dragged_files.emplace_back(cannonical_path);
                        }
                    }
                } break;
                default: {
                    std::cerr << "Unknown drag-and-drop format "
                              << storage.tymed << std::endl;
                } break;
            }

            if (storage.pUnkForRelease) {
                storage.pUnkForRelease->Release();
            }
        }
    }

    if (dragged_files.empty()) {
        std::cerr
            << "Plugin wanted to drag-and-drop, but didn't specify any files"
            << std::endl;
        return;
    }

    std::cerr << "Plugin wanted to drag-and-drop " << dragged_files.size()
              << (dragged_files.size() == 1 ? " file:" : " files:")
              << std::endl;
    for (const auto& file : dragged_files) {
        std::cerr << "- " << file << std::endl;
    }

    // This shouldn't be possible, but you can never be too sure!
    if (instance_reference_count <= 0 || !instance) {
        std::cerr << "Drag-and-drop proxy has not yet been initialized"
                  << std::endl;
        return;
    }

    try {
        instance->begin_xdnd(dragged_files, hwnd);
    } catch (const std::exception& error) {
        std::cerr << "XDND initialization failed:" << std::endl;
        std::cerr << error.what() << std::endl;
    }
}
