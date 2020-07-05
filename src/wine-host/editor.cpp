// yabridge: a Wine VST bridge
// Copyright (C) 2020  Robbert van der Helm
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

#include "editor.h"

// The Win32 API requires you to hardcode identifiers for tiemrs
constexpr size_t idle_timer_id = 1337;

/**
 * The most significant bit in an event's response type is used to indicate
 * whether the event source.
 */
constexpr uint16_t event_type_mask = ((1 << 7) - 1);

/**
 * Find the topmost window (i.e. the window before the root window in the window
 * tree) starting from a certain window.
 *
 * @param x11_connection The X11 connection to use.
 * @param starting_at The window we want to know the topmost window of.
 *
 * @return Either `starting_at`, if its parent is already the root window, or
 *   another another window that has `starting_at` as its descendent.
 */
xcb_window_t find_topmost_window(xcb_connection_t& x11_connection,
                                 xcb_window_t starting_at);
/**
 * Compute the size a window would have to be to be allowed to fullscreened on
 * any of the connected screens.
 */
Size get_maximum_screen_dimensions(xcb_connection_t& x11_connection);
/**
 * Return the X11 window handle for the window if it's currently open.
 */
xcb_window_t get_x11_handle(HWND win32_handle);
ATOM register_window_class(std::string window_class_name);

WindowClass::WindowClass(const std::string& name)
    : atom(register_window_class(name)) {}

WindowClass::~WindowClass() {
    UnregisterClass(reinterpret_cast<LPCSTR>(atom), GetModuleHandle(nullptr));
}

Editor::Editor(const std::string& window_class_name,
               AEffect* effect,
               const size_t parent_window_handle)
    : x11_connection(xcb_connect(nullptr, nullptr), xcb_disconnect),
      client_area(get_maximum_screen_dimensions(*x11_connection)),
      window_class(window_class_name),
      // Create a window without any decoratiosn for easy embedding. The
      // combination of `WS_EX_TOOLWINDOW` and `WS_POPUP` causes the window to
      // be drawn without any decorations (making resizes behave as you'd
      // expect) and also causes mouse coordinates to be relative to the window
      // itself.
      win32_handle(CreateWindowEx(WS_EX_TOOLWINDOW,
                                  reinterpret_cast<LPCSTR>(window_class.atom),
                                  "yabridge plugin",
                                  WS_POPUP,
                                  CW_USEDEFAULT,
                                  CW_USEDEFAULT,
                                  client_area.width,
                                  client_area.height,
                                  nullptr,
                                  nullptr,
                                  GetModuleHandle(nullptr),
                                  this),
                   DestroyWindow),
      idle_timer(win32_handle.get(), idle_timer_id, 100),
      parent_window(parent_window_handle),
      child_window(get_x11_handle(win32_handle.get())),
      topmost_window(find_topmost_window(*x11_connection, parent_window)),
      // Needed to send update messages on a timer
      plugin(effect) {
    // Because we're not using XEmbed Wine will interpret any local coordinates
    // as global coordinates. To work around this we'll tell the Wine window
    // it's located at its actual coordinates on screen rather than somewhere
    // within. For robustness's sake this should be done both when the actual
    // window the Wine window is embedded in (which may not be the parent
    // window) is moved or resized, and when the user moves his mouse over the
    // window. We also want to set keyboard focus when the user clicks on the
    // Windows since Bitwig 3.2 now explicitely requires this.
    const uint32_t topmost_event_mask = XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    xcb_change_window_attributes(x11_connection.get(), topmost_window,
                                 XCB_CW_EVENT_MASK, &topmost_event_mask);
    xcb_flush(x11_connection.get());
    const uint32_t parent_event_mask =
        XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_ENTER_WINDOW;
    xcb_change_window_attributes(x11_connection.get(), parent_window,
                                 XCB_CW_EVENT_MASK, &parent_event_mask);
    xcb_flush(x11_connection.get());

    // Embed the Win32 window into the window provided by the host. Instead of
    // using the XEmbed protocol, we'll register a few events and manage the
    // child window ourselves. This is a hack to work around the issue's
    // described in `Editor`'s docstring'.
    xcb_reparent_window(x11_connection.get(), child_window, parent_window, 0,
                        0);
    xcb_map_window(x11_connection.get(), child_window);
    xcb_flush(x11_connection.get());

    ShowWindow(win32_handle.get(), SW_SHOWNORMAL);
}

Editor::~Editor() {
    // Wine will wait for the parent window to properly delete the window during
    // `DestroyWindow()`. Instead of implementing this behavior ourselves we
    // just reparent the window back to the window root and let the WM handle
    // it.
    xcb_window_t root =
        xcb_setup_roots_iterator(xcb_get_setup(x11_connection.get()))
            .data->root;

    xcb_reparent_window(x11_connection.get(), child_window, root, 0, 0);
    xcb_flush(x11_connection.get());

    // FIXME: I have no idea why, but for some reason the window still hangs
    //        some of the times without manually resetting the
    //        `std::unique_ptr`` to the window handle` (which calls
    //        `DestroyWindow()`), even though the behavior should be identical
    //        without this line.
    win32_handle.reset();
}

void Editor::send_idle_event() {
    plugin->dispatcher(plugin, effEditIdle, 0, 0, nullptr, 0);
}

void Editor::handle_win32_events() const {
    MSG msg;

    // The null value for the second argument is needed to handle interaction
    // with child GUI components
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        // This timer would periodically send `effEditIdle` events so the editor
        // remains responsive even during blocking GUI operations such as open
        // dropdowns or message boxes. This is only needed when the GUI is
        // actually blocked and it will be dispatched by the messaging loop of
        // the blocking GUI component. Since we're not touching the
        // `effEditIdle` event sent by the host we can always filter this timer
        // event out in this event loop.
        if (msg.message == WM_TIMER && msg.wParam == idle_timer_id &&
            msg.hwnd == win32_handle.get()) {
            continue;
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void Editor::handle_x11_events() const {
    // TODO: Initiating drag-and-drop in Serum _sometimes_ causes the GUI to
    //       update while dragging while other times it does not. From all the
    //       plugins I've tested this only happens in Serum though.
    xcb_generic_event_t* generic_event;
    while ((generic_event = xcb_poll_for_event(x11_connection.get())) !=
           nullptr) {
        switch (generic_event->response_type & event_type_mask) {
            // We're listening for `ConfigureNotify` events on the topmost
            // window before the root window, i.e. the window that's actually
            // going to get dragged around the by the user. In most cases this
            // is the same as `parent_window`. When either this window gets
            // moved, or when the user moves his mouse over our window, the
            // local coordinates should be updated. The additional `EnterWindow`
            // check is sometimes necessary for using multiple editor windows
            // within a single plugin group.
            case XCB_CONFIGURE_NOTIFY:
            case XCB_ENTER_NOTIFY:
                fix_local_coordinates();
                break;
            case XCB_FOCUS_IN:
                fix_local_coordinates();

                // Explicitely request input focus when the user clicks on the
                // window. This is needed for Bitwig Studio 3.2, as the parent
                // window now captures all keyboard events and forwards them to
                // the main Bitwig Studio window instead of allowing the child
                // window to handle those events.
                xcb_set_input_focus(x11_connection.get(),
                                    XCB_INPUT_FOCUS_PARENT, child_window,
                                    XCB_CURRENT_TIME);
                xcb_flush(x11_connection.get());
                break;
        }

        free(generic_event);
    }
}

void Editor::fix_local_coordinates() const {
    // We're purposely not using XEmbed. This has the consequence that wine
    // still thinks that any X and Y coordinates are relative to the x11 window
    // root instead of the parent window provided by the DAW, causing all sorts
    // of GUI interactions to break. To alleviate this we'll just lie to Wine
    // and tell it that it's located at the parent window's location on the root
    // window. We also will keep the child window at its largest possible size
    // to allow for smooth resizing. This works because the embedding hierarchy
    // is DAW window -> Win32 window (created in this class) -> VST plugin
    // window created by the plugin itself. In this case it doesn't matter that
    // the Win32 window is larger than the part of the client area the plugin
    // draws to since any excess will be clipped off by the parent window.
    const auto query_cookie =
        xcb_query_tree(x11_connection.get(), parent_window);
    xcb_window_t root =
        xcb_query_tree_reply(x11_connection.get(), query_cookie, nullptr)->root;

    // We can't directly use the `event.x` and `event.y` coordinates because the
    // parent window may also be embedded inside another window.
    const auto translate_cookie = xcb_translate_coordinates(
        x11_connection.get(), parent_window, root, 0, 0);
    const xcb_translate_coordinates_reply_t* translated_coordinates =
        xcb_translate_coordinates_reply(x11_connection.get(), translate_cookie,
                                        nullptr);

    xcb_configure_notify_event_t translated_event{};
    translated_event.response_type = XCB_CONFIGURE_NOTIFY;
    translated_event.event = child_window;
    translated_event.window = child_window;
    // This should be set to the same sizes the window was created on. Since
    // we're not using `SetWindowPos` to resize the Window, Wine can get a bit
    // confused when we suddenly report a different client area size. Without
    // this certain plugins (such as those by Valhalla DSP) would break.
    translated_event.width = client_area.width;
    translated_event.height = client_area.height;
    translated_event.x = translated_coordinates->dst_x;
    translated_event.y = translated_coordinates->dst_y;

    xcb_send_event(
        x11_connection.get(), false, child_window,
        XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
        reinterpret_cast<char*>(&translated_event));
    xcb_flush(x11_connection.get());
}

LRESULT CALLBACK window_proc(HWND handle,
                             UINT message,
                             WPARAM wParam,
                             LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            const auto window_parameters =
                reinterpret_cast<CREATESTRUCT*>(lParam);
            const auto editor =
                static_cast<Editor*>(window_parameters->lpCreateParams);
            if (!editor) {
                break;
            }

            // Sent when the window is first being created. `lParam` here
            // contains the last argument of `CreateWindowEx`, which was a
            // pointer to the `Editor` object. We need to attach this to the
            // window handle so we can access our VST plugin instance later.
            SetWindowLongPtr(handle, GWLP_USERDATA,
                             reinterpret_cast<size_t>(editor));
        } break;
        case WM_TIMER: {
            auto editor = reinterpret_cast<Editor*>(
                GetWindowLongPtr(handle, GWLP_USERDATA));
            if (!editor || wParam != idle_timer_id) {
                break;
            }

            // We'll send idle messages on a timer. This way the plugin will get
            // keep periodically updating its editor either when the host sends
            // `effEditIdle` themself, or periodically when the GUI is being
            // blocked by a dropdown or a message box.
            editor->send_idle_event();
            return 0;
        } break;
    }

    return DefWindowProc(handle, message, wParam, lParam);
}

xcb_window_t find_topmost_window(xcb_connection_t& x11_connection,
                                 xcb_window_t starting_at) {
    xcb_window_t current_window = starting_at;

    xcb_query_tree_cookie_t query_cookie =
        xcb_query_tree(&x11_connection, starting_at);
    xcb_query_tree_reply_t* query_reply =
        xcb_query_tree_reply(&x11_connection, query_cookie, nullptr);
    xcb_window_t root = query_reply->root;
    while (query_reply->parent != root) {
        current_window = query_reply->parent;

        query_cookie = xcb_query_tree(&x11_connection, current_window);
        query_reply =
            xcb_query_tree_reply(&x11_connection, query_cookie, nullptr);
    }

    return current_window;
}

Size get_maximum_screen_dimensions(xcb_connection_t& x11_connection) {
    xcb_screen_iterator_t iter =
        xcb_setup_roots_iterator(xcb_get_setup(&x11_connection));

    // Find the maximum dimensions the window would have to be to be able to be
    // fullscreened on any screen, disregarding the possibility that someone
    // would try to stretch the window accross all displays (because who would
    // do such a thing?)
    Size maximum_screen_size{};
    while (iter.rem > 0) {
        maximum_screen_size.width =
            std::max(maximum_screen_size.width, iter.data->width_in_pixels);
        maximum_screen_size.height =
            std::max(maximum_screen_size.height, iter.data->height_in_pixels);

        xcb_screen_next(&iter);
    }

    return maximum_screen_size;
}

xcb_window_t get_x11_handle(HWND win32_handle) {
    return reinterpret_cast<size_t>(
        GetProp(win32_handle, "__wine_x11_whole_window"));
}

ATOM register_window_class(std::string window_class_name) {
    WNDCLASSEX window_class{};

    window_class.cbSize = sizeof(WNDCLASSEX);
    window_class.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = GetModuleHandle(nullptr);
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.lpszClassName = window_class_name.c_str();

    return RegisterClassEx(&window_class);
}
