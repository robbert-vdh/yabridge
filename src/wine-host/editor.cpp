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

#include <iostream>

// The Win32 API requires you to hardcode identifiers for tiemrs
constexpr size_t idle_timer_id = 1337;

/**
 * The most significant bit in an event's response type is used to indicate
 * whether the event source.
 */
constexpr uint16_t event_type_mask = ((1 << 7) - 1);

/**
 * The name of the X11 property on the root window used to denote the active
 * window in EWMH compliant window managers.
 */
constexpr char active_window_property_name[] = "_NET_ACTIVE_WINDOW";

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
 * Check whether `child` is a descendant of `parent` or the same window. Used
 * during focus checks to only grab focus when needed.
 *
 * @param x11_connection The X11 connection to use.
 * @param child The potential child window.
 * @param parent The potential parent window.
 *
 * @return Whether `child` is a descendant of or the same window as `parent.`
 */
bool is_child_window_or_same(xcb_connection_t& x11_connection,
                             xcb_window_t child,
                             xcb_window_t parent);
/**
 * Compute the size a window would have to be to be allowed to fullscreened on
 * any of the connected screens.
 */
Size get_maximum_screen_dimensions(xcb_connection_t& x11_connection);
/**
 * Get the root window for the specified window. The returned root window will
 * depend on the screen the window is on.
 */
xcb_window_t get_root_window(xcb_connection_t& x11_connection,
                             xcb_window_t window);
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

Editor::Editor(const Configuration& config,
               const std::string& window_class_name,
               const size_t parent_window_handle,
               AEffect* effect)
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
      // If `config.editor_double_embed` is set, then we'll also create a child
      // window in `win32_child_handle`. If we do this before calling
      // `ShowWindow()` on `win32_handle` we'll run into X11 errors.
      idle_timer(win32_handle.get(), idle_timer_id, 100),
      parent_window(parent_window_handle),
      wine_window(get_x11_handle(win32_handle.get())),
      topmost_window(find_topmost_window(*x11_connection, parent_window)),
      // Needed to send update messages on a timer
      plugin(effect) {
    xcb_generic_error_t* error;

    // Used for input focus grabbing to only grab focus when the window is
    // active.
    const xcb_intern_atom_cookie_t atom_cookie = xcb_intern_atom(
        x11_connection.get(), true, strlen(active_window_property_name),
        active_window_property_name);
    xcb_intern_atom_reply_t* atom_reply =
        xcb_intern_atom_reply(x11_connection.get(), atom_cookie, &error);
    assert(!error);

    // In case the atom does not exist or the WM does not support this hint,
    // we'll print a warning and fall back to grabbing focus when the user
    // clicks on the window (which should trigger a `WM_PARENTNOTIFY`).
    active_window_property = atom_reply->atom;
    free(atom_reply);
    if (!supports_ewmh_active_window()) {
        std::cout << "WARNING: The current window manager does not support the"
                  << std::endl;
        std::cout << "         '" << active_window_property_name
                  << "' property. Falling back to a" << std::endl;
        std::cout << "         less reliable keyboard input grabbing method."
                  << std::endl;
    }

    // Because we're not using XEmbed, Wine will interpret any local coordinates
    // as global coordinates. To work around this we'll tell the Wine window
    // it's located at its actual coordinates on screen rather than somewhere
    // within. For robustness's sake this should be done both when the actual
    // window the Wine window is embedded in (which may not be the parent
    // window) is moved or resized, and when the user moves his mouse over the
    // window because this is sometimes needed for plugin groups. We also listen
    // for EnterNotify and LeaveNotify events on the Wine window so we can grab
    // and release input focus as necessary.
    const uint32_t topmost_event_mask = XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    xcb_change_window_attributes(x11_connection.get(), topmost_window,
                                 XCB_CW_EVENT_MASK, &topmost_event_mask);
    xcb_flush(x11_connection.get());
    const uint32_t parent_event_mask = XCB_EVENT_MASK_FOCUS_CHANGE |
                                       XCB_EVENT_MASK_ENTER_WINDOW |
                                       XCB_EVENT_MASK_LEAVE_WINDOW;
    xcb_change_window_attributes(x11_connection.get(), parent_window,
                                 XCB_CW_EVENT_MASK, &parent_event_mask);
    xcb_flush(x11_connection.get());

    // Embed the Win32 window into the window provided by the host. Instead of
    // using the XEmbed protocol, we'll register a few events and manage the
    // child window ourselves. This is a hack to work around the issue's
    // described in `Editor`'s docstring'.
    xcb_reparent_window(x11_connection.get(), wine_window, parent_window, 0, 0);
    xcb_flush(x11_connection.get());

    ShowWindow(win32_handle.get(), SW_SHOWNORMAL);
    if (config.editor_double_embed) {
        // As explained above, we can't do this directly in the initializer list
        win32_child_handle = std::unique_ptr<std::remove_pointer_t<HWND>,
                                             decltype(&DestroyWindow)>(
            CreateWindowEx(
                WS_EX_TOOLWINDOW, reinterpret_cast<LPCSTR>(window_class.atom),
                "yabridge plugin child", WS_CHILD, CW_USEDEFAULT, CW_USEDEFAULT,
                client_area.width, client_area.height, win32_handle.get(),
                nullptr, GetModuleHandle(nullptr), this),
            DestroyWindow);

        ShowWindow(win32_child_handle->get(), SW_SHOWNORMAL);
    }

    // HACK: I can't seem to figure why the initial reparent would fail on this
    //       particular i3 config in a way that I'm unable to reproduce, but if
    //       it doesn't work the first time, just keep trying!
    //
    //       https://github.com/robot-vdh/yabridge/issues/40
    xcb_reparent_window(x11_connection.get(), wine_window, parent_window, 0, 0);
    xcb_flush(x11_connection.get());
}

Editor::~Editor() {
    // Wine will wait for the parent window to properly delete the window during
    // `DestroyWindow()`. Instead of implementing this behavior ourselves we
    // just reparent the window back to the window root and let the WM handle
    // it.
    xcb_window_t root =
        xcb_setup_roots_iterator(xcb_get_setup(x11_connection.get()))
            .data->root;

    xcb_reparent_window(x11_connection.get(), wine_window, root, 0, 0);
    xcb_flush(x11_connection.get());

    // FIXME: I have no idea why, but for some reason the window still hangs
    //        some of the times without manually resetting the
    //        `std::unique_ptr`` to the window handle` (which calls
    //        `DestroyWindow()`), even though the behavior should be identical
    //        without this line.
    win32_child_handle.reset();
    win32_handle.reset();
}

HWND Editor::get_win32_handle() const {
    if (win32_child_handle) {
        return win32_child_handle->get();
    } else {
        return win32_handle.get();
    }
}

void Editor::send_idle_event() {
    plugin->dispatcher(plugin, effEditIdle, 0, 0, nullptr, 0);
}

void Editor::handle_win32_events() const {
    MSG msg;

    // The null value for the second argument is needed to handle interaction
    // with child GUI components. So far limiting this to `max_win32_messages`
    // messages has only been needed for Waves plugins as they otherwise cause
    // an infinite message loop.
    for (int i = 0;
         i < max_win32_messages && PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE);
         i++) {
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
                fix_local_coordinates();
                break;
            // We want to grab keyboard input focus when the user hovers over
            // our embedded Wine window AND that window is a child of the
            // currently active window. This ensures that the behavior is
            // similar to what you'd expect of a native application, without
            // grabbing input focus when accidentally hovering over a yabridge
            // window in the background.
            // The `FocusIn` is needed for when returning to the main plugin
            // window after closing a dialog, since that often won't trigger an
            // `EnterNotify'.
            case XCB_ENTER_NOTIFY:
            case XCB_FOCUS_IN:
                fix_local_coordinates();

                // In case the WM somehow does not support `_NET_ACTIVE_WINDOW`,
                // a more naive focus grabbing method implemented in the
                // `WM_PARENTNOTIFY` handler will be used.
                if (supports_ewmh_active_window() && is_wine_window_active()) {
                    set_input_focus(true);
                }
                break;
            // When the user moves their mouse away from the Wine window _while
            // the window provided by the host it is contained in is still
            // active_, we will give back keyboard focus to that window. This
            // for instance allows you to still use the search bar in REAPER's
            // FX window. This distinction is important, because we do not want
            // to mess with keyboard focus when hovering over the window while
            // for instance a dialog is open.
            case XCB_LEAVE_NOTIFY: {
                const auto event =
                    reinterpret_cast<xcb_leave_notify_event_t*>(generic_event);

                // This extra check for the `NonlinearVirtual` detail is
                // important (see
                // https://www.x.org/releases/X11R7.5/doc/x11proto/proto.html
                // for more information on what this actually means). I've only
                // seen this issue with the Tokyo Dawn Records plugins, but a
                // plugin may create a popup window that acts as a dropdown
                // without actually activating that window (unlike with an
                // actual Win32 dropdown menu). Without this check these fake
                // dropdowns would immediately close when hovering over them.
                if (event->detail != XCB_NOTIFY_DETAIL_NONLINEAR_VIRTUAL &&
                    supports_ewmh_active_window() && is_wine_window_active()) {
                    set_input_focus(false);
                }
            } break;
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
    const xcb_window_t root = get_root_window(*x11_connection, parent_window);

    // We can't directly use the `event.x` and `event.y` coordinates because the
    // parent window may also be embedded inside another window.
    xcb_generic_error_t* error;
    const xcb_translate_coordinates_cookie_t translate_cookie =
        xcb_translate_coordinates(x11_connection.get(), parent_window, root, 0,
                                  0);
    xcb_translate_coordinates_reply_t* translated_coordinates =
        xcb_translate_coordinates_reply(x11_connection.get(), translate_cookie,
                                        &error);
    assert(!error);

    xcb_configure_notify_event_t translated_event{};
    translated_event.response_type = XCB_CONFIGURE_NOTIFY;
    translated_event.event = wine_window;
    translated_event.window = wine_window;
    // This should be set to the same sizes the window was created on. Since
    // we're not using `SetWindowPos` to resize the Window, Wine can get a bit
    // confused when we suddenly report a different client area size. Without
    // this certain plugins (such as those by Valhalla DSP) would break.
    translated_event.width = client_area.width;
    translated_event.height = client_area.height;
    translated_event.x = translated_coordinates->dst_x;
    translated_event.y = translated_coordinates->dst_y;
    free(translated_coordinates);

    xcb_send_event(
        x11_connection.get(), false, wine_window,
        XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
        reinterpret_cast<char*>(&translated_event));
    xcb_flush(x11_connection.get());
}

void Editor::set_input_focus(bool grab) const {
    // Explicitly request input focus when the user interacts with the window.
    // Without this `topmost_window` will capture all keyboard events in most
    // hosts. Ideally we would just do this whenever the child window calls
    // `SetFocus()` (or no handling should be necessary), but as far as I'm
    // aware there is no way to do this. Right now we will grab input focus when
    // the user hovers over the Wine window while the window it is contained in
    // (the one provided by the host) is active. Keyboard focus will be given
    // back to that window when the user moves their mouse outside of the Wine
    // window while the host's window is still active (that's an important
    // detail, since plugins may have dialogs).
    xcb_set_input_focus(x11_connection.get(), XCB_INPUT_FOCUS_PARENT,
                        grab ? parent_window : topmost_window,
                        XCB_CURRENT_TIME);
    xcb_flush(x11_connection.get());
}

bool Editor::is_wine_window_active() const {
    if (!supports_ewmh_active_window()) {
        return false;
    }

    // We will only grab focus when the Wine window is active. To do this we'll
    // read the `_NET_ACTIVE_WINDOW` property from the root window (which can
    // change when the window gets moved to another screen, so we won't cache
    // this).
    const xcb_window_t root_window =
        get_root_window(*x11_connection, wine_window);

    xcb_generic_error_t* error;
    const xcb_get_property_cookie_t property_cookie =
        xcb_get_property(x11_connection.get(), false, root_window,
                         active_window_property, XCB_ATOM_WINDOW, 0, 1);
    xcb_get_property_reply_t* property_reply =
        xcb_get_property_reply(x11_connection.get(), property_cookie, &error);
    assert(!error);
    const xcb_window_t active_window =
        *static_cast<xcb_window_t*>(xcb_get_property_value(property_reply));
    free(property_reply);

    return is_child_window_or_same(*x11_connection, wine_window, active_window);
}

bool Editor::supports_ewmh_active_window() const {
    if (supports_ewmh_active_window_cache) {
        return *supports_ewmh_active_window_cache;
    }

    // It could be that the `_NET_ACTIVE_WINDOW` atom exists (because it was
    // created by another application) but that the root window does not have
    // the property
    if (active_window_property == XCB_ATOM_NONE) {
        supports_ewmh_active_window_cache = false;
        return false;
    }

    const xcb_window_t root_window =
        get_root_window(*x11_connection, wine_window);

    // If the `_NET_ACTIVE_WINDOW` property does not exist on the root window,
    // the returned property type will be `XCB_ATOM_NONE` as specified in the
    // X11 manual
    xcb_generic_error_t* error;
    const xcb_get_property_cookie_t property_cookie =
        xcb_get_property(x11_connection.get(), false, root_window,
                         active_window_property, XCB_ATOM_WINDOW, 0, 1);
    xcb_get_property_reply_t* property_reply =
        xcb_get_property_reply(x11_connection.get(), property_cookie, &error);
    assert(!error);
    bool active_window_property_exists =
        property_reply->format != XCB_ATOM_NONE;
    free(property_reply);

    supports_ewmh_active_window_cache = active_window_property_exists;
    return active_window_property_exists;
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
        // In case the WM does not support the EWMH active window property,
        // we'll fall back to grabbing focus when the user clicks on the window
        // by listening to the generated `WM_PARENTNOTIFY` messages. Otherwise
        // we have some more sophisticated behaviour using `EnterNotify` and
        // `LeaveNotify` X11 events. This will only be necessary for very
        // barebones window managers.
        case WM_PARENTNOTIFY: {
            auto editor = reinterpret_cast<Editor*>(
                GetWindowLongPtr(handle, GWLP_USERDATA));
            if (!editor || editor->supports_ewmh_active_window()) {
                break;
            }

            editor->set_input_focus(true);
        } break;
    }

    return DefWindowProc(handle, message, wParam, lParam);
}

xcb_window_t find_topmost_window(xcb_connection_t& x11_connection,
                                 xcb_window_t starting_at) {
    xcb_window_t current_window = starting_at;

    xcb_generic_error_t* error;
    xcb_query_tree_cookie_t query_cookie =
        xcb_query_tree(&x11_connection, starting_at);
    xcb_query_tree_reply_t* query_reply =
        xcb_query_tree_reply(&x11_connection, query_cookie, &error);
    assert(!error);

    xcb_window_t root = query_reply->root;
    while (query_reply->parent != root) {
        current_window = query_reply->parent;

        free(query_reply);
        query_cookie = xcb_query_tree(&x11_connection, current_window);
        query_reply =
            xcb_query_tree_reply(&x11_connection, query_cookie, &error);
        assert(!error);
    }

    free(query_reply);
    return current_window;
}

bool is_child_window_or_same(xcb_connection_t& x11_connection,
                             xcb_window_t child,
                             xcb_window_t parent) {
    xcb_window_t current_window = child;

    xcb_generic_error_t* error;
    xcb_query_tree_cookie_t query_cookie =
        xcb_query_tree(&x11_connection, child);
    xcb_query_tree_reply_t* query_reply =
        xcb_query_tree_reply(&x11_connection, query_cookie, &error);
    assert(!error);

    while (query_reply->parent != XCB_NONE) {
        if (current_window == parent) {
            free(query_reply);
            return true;
        }

        current_window = query_reply->parent;

        free(query_reply);
        query_cookie = xcb_query_tree(&x11_connection, current_window);
        query_reply =
            xcb_query_tree_reply(&x11_connection, query_cookie, &error);
        assert(!error);
    }

    free(query_reply);
    return false;
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

xcb_window_t get_root_window(xcb_connection_t& x11_connection,
                             xcb_window_t window) {
    xcb_generic_error_t* error;
    const xcb_query_tree_cookie_t query_cookie =
        xcb_query_tree(&x11_connection, window);
    xcb_query_tree_reply_t* query_reply =
        xcb_query_tree_reply(&x11_connection, query_cookie, &error);
    assert(!error);

    const xcb_window_t root = query_reply->root;
    free(query_reply);

    return root;
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
