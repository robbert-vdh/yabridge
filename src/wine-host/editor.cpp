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

// TODO: The (maximum) client area is now set at 1440p, to prevent unnecessary
//       overhead and to support fullscreen windows at 4k resolutions we should
//       just use the dimensions of the X11 root window instead.
/**
 * The initial and maximum width of the Wine window hosting the plugin's editor
 * window. This is set at a fixed size to make window resizing feel native.
 */
constexpr uint16_t client_area_width = 2560;
/**
 * The initial and maximum width of the Wine window hosting the plugin's editor
 * window. This is set at a fixed size to make window resizing feel native.
 */
constexpr uint16_t client_area_height = 1440;

/**
 * Return the X11 window handle for the window if it's currently open.
 */
xcb_window_t get_x11_handle(HWND win32_handle);

ATOM register_window_class(std::string window_class_name);

WindowClass::WindowClass(std::string name)
    : atom(register_window_class(name)) {}

WindowClass::~WindowClass() {
    UnregisterClass(reinterpret_cast<LPCSTR>(atom), GetModuleHandle(nullptr));
}

Editor::Editor(const std::string& window_class_name,
               AEffect* effect,
               const size_t parent_window_handle)
    : window_class(window_class_name),
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
                                  client_area_width,
                                  client_area_height,
                                  nullptr,
                                  nullptr,
                                  GetModuleHandle(nullptr),
                                  this),
                   DestroyWindow),
      parent_window(parent_window_handle),
      child_window(get_x11_handle(win32_handle.get())),
      // Needed to send update messages on a timer
      plugin(effect),
      x11_connection(xcb_connect(nullptr, nullptr), xcb_disconnect) {
    // The Win32 API will block the `DispatchMessage` call when opening e.g. a
    // dropdown, but it will still allow timers to be run so the GUI can still
    // update in the background. Because of this we send `effEditIdle` to the
    // plugin on a timer. The refresh rate is purposely fairly low since we
    // we'll also trigger this manually in `Editor::handle_events()` whenever
    // the plugin is not busy.
    SetTimer(win32_handle.get(), idle_timer_id, 100, nullptr);

    // See the x11 events part of `editor::handle_events`
    const uint32_t parent_event_mask = XCB_EVENT_MASK_STRUCTURE_NOTIFY;
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

void Editor::handle_events() {
    send_idle_event();

    // The null value for the second argument is needed to handle interaction
    // with child GUI components
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        // This timer would periodically send `effEditIdle` events so the editor
        // remains responsive even during blocking GUI operations such as open
        // dropdowns or message boxes. We filter it out here because we will
        // send sent the event manually every time the host calls
        // `effEditIdle()`. It will still be fired implicitely when the GUI
        // thread gets blocked.
        if (msg.message == WM_TIMER && msg.wParam == idle_timer_id &&
            msg.hwnd == win32_handle.get()) {
            continue;
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Handle X11 events
    // TODO: Initiating drag-and-drop _sometimes_ causes the GUI to update while
    //       dragging while other times it does not. Not sure why this is
    //       happening. Forwarding XDND requests manually is very clunky and
    //       causes different problems.
    xcb_generic_event_t* generic_event;
    while ((generic_event = xcb_poll_for_event(x11_connection.get())) !=
           nullptr) {
        switch (generic_event->response_type & event_type_mask) {
            case XCB_CONFIGURE_NOTIFY: {
                xcb_configure_notify_event_t event =
                    *reinterpret_cast<xcb_configure_notify_event_t*>(
                        generic_event);
                if (event.window != parent_window) {
                    break;
                }

                // We're purposely not using XEmbed. This has the consequence
                // that wine still thinks that any X and Y coordinates are
                // relative to the x11 window root instead of the parent window
                // provided by the DAW, causing all sorts of GUI interactions to
                // break. To alleviate this we'll just lie to Wine and tell it
                // that it's located at the parent window's location. We'll only
                // send the event instead of actually configuring the window.
                // NOTE: We're not actually using `SetWindowPos()` to resize the
                //       window. The editor's client area will likely always be
                //       big enough Since we specified the Window to be
                //       2560x1440 pixels large at the time of its creation.
                //       This works because the embedding hierarchy is DAW
                //       window -> Win32 window (created in this class) -> VST
                //       plugin window created by the plugin itself. This makes
                //       the drag-to-resize functionality many plugin editors
                //       have feel smooth and native.
                xcb_configure_notify_event_t translated_event{};
                translated_event.response_type = XCB_CONFIGURE_NOTIFY;
                translated_event.event = child_window;
                translated_event.window = child_window;
                translated_event.width = client_area_width;
                translated_event.height = client_area_height;
                translated_event.x = event.x;
                translated_event.y = event.y;

                xcb_send_event(x11_connection.get(), false, child_window,
                               XCB_EVENT_MASK_STRUCTURE_NOTIFY |
                                   XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                               reinterpret_cast<char*>(&translated_event));
                xcb_flush(x11_connection.get());
            } break;
        }
        free(generic_event);
    }
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
            if (editor == nullptr) {
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
            if (editor == nullptr || wParam != idle_timer_id) {
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
