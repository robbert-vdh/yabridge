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

Editor::Editor(std::string window_class_name)
    : window_class(register_window_class(window_class_name)),
      x11_connection(xcb_connect(nullptr, nullptr), &xcb_disconnect) {}

HWND Editor::open(AEffect* effect) {
    // Create a window without any decoratiosn for easy embedding. The
    // combination of `WS_EX_TOOLWINDOW` and `WS_POPUP` causes the window to be
    // drawn without any decorations (making resizes behave as you'd expect) and
    // also causes mouse coordinates to be relative to the window itself.
    win32_handle =
        std::unique_ptr<std::remove_pointer_t<HWND>, decltype(&DestroyWindow)>(
            CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_ACCEPTFILES,
                           reinterpret_cast<LPCSTR>(window_class),
                           "yabridge plugin", WS_POPUP, CW_USEDEFAULT,
                           CW_USEDEFAULT, client_area_width, client_area_height,
                           nullptr, nullptr, GetModuleHandle(nullptr), this),
            &DestroyWindow);

    // Needed to send update messages on a timer
    plugin = effect;

    // The Win32 API will block the `DispatchMessage` call when opening e.g. a
    // dropdown, but it will still allow timers to be run so the GUI can still
    // update in the background. Because of this we send `effEditIdle` to the
    // plugin on a timer. The refresh rate is purposely fairly low since we
    // we'll also trigger this manually in `Editor::handle_events()` whenever
    // the plugin is not busy.
    SetTimer(win32_handle->get(), idle_timer_id, 100, nullptr);

    return win32_handle->get();
}

void Editor::close() {
    // RAII will destroy the window and tiemrs for us
    win32_handle = std::nullopt;

    // TODO: Do we need to do something on the X11 side or does the host do
    //       everything for us?
}

bool Editor::embed_into(const size_t parent_window_handle) {
    if (!win32_handle.has_value()) {
        return false;
    }

    child_window = get_x11_handle(win32_handle->get());
    parent_window = parent_window_handle;

    // See the X11 events part of `Editor::handle_events`.
    // const uint32_t child_event_mask = XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    // xcb_change_window_attributes(x11_connection.get(), child_window,
    //                              XCB_CW_EVENT_MASK, &child_event_mask);
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

    ShowWindow(win32_handle->get(), SW_SHOWNORMAL);

    return true;
}

void Editor::handle_events() {
    // Process any remaining events, otherwise we won't be able to interact with
    // the window
    if (win32_handle.has_value()) {
        bool gui_was_updated = false;
        MSG msg;

        // The second argument has to be null since we not only want to handle
        // events for this window but also for all child windows (i.e.
        // dropdowns). I spent way longer debugging this than I want to admit.
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            if (msg.message == WM_TIMER && msg.wParam == idle_timer_id) {
                gui_was_updated = true;
            }
        }

        // Make sure that the GUI always gets updated at least once for every
        // `effEditIdle` call the host has sent to improve responsiveness when
        // the GUI isn't being blocked.
        if (!gui_was_updated) {
            SendMessage(win32_handle->get(), WM_TIMER, idle_timer_id, 0);
        }

        // Handle X11 events
        // TODO: Check if we should forward other events mostly to prevent
        //       unnecessary GUI processing in the background
        // TODO: Check whether drag and drop works out of the box
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

                    // We're purposely not using XEmbed. This has the
                    // consequence that wine still thinks that any X and Y
                    // coordinates are relative to the x11 window root instead
                    // of the parent window provided by the DAW, causing all
                    // sorts of GUI interactions to break. To alleviate this
                    // we'll just lie to Wine and tell it that it's located at
                    // the parent window's location. We'll only send the event
                    // instead of actually configuring the window.
                    // NOTE: We're not actually using `SetWindowPos()` to resize
                    //       the window. The editor's client area will likely
                    //       always be big enough Since we specified the Window
                    //       to be 2560x1440 pixels large at the time of its
                    //       creation. This works because the embedding
                    //       hierarchy is DAW window -> Win32 window (created in
                    //       this class) -> VST plugin window created by the
                    //       plugin itself. This makes the drag-to-resize
                    //       functionality many plugin editors have feel smooth
                    //       and native.
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
            // these either when the host sends `effEditIdle` themself, or
            // periodically when the GUI is being blocked by a dropdown or a
            // message box.
            editor->plugin->dispatcher(editor->plugin, effEditIdle, 0, 0,
                                       nullptr, 0);
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
