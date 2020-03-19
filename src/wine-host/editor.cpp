#include "editor.h"

ATOM register_window_class(std::string window_class_name);

Editor::Editor(std::string window_class_name)
    : window_class(register_window_class(window_class_name)),
      x11_connection(xcb_connect(nullptr, nullptr), &xcb_disconnect) {}

HWND Editor::open() {
    // Create a window without any decoratiosn for easy embedding. The
    // combination of `WS_EX_TOOLWINDOW` and `WS_POPUP` causes the window to be
    // drawn without any decorations (making resizes behave as you'd expect) and
    // also causes mouse coordinates to be relative to the window itself.
    win32_handle =
        std::unique_ptr<std::remove_pointer_t<HWND>, decltype(&DestroyWindow)>(
            CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_ACCEPTFILES,
                           reinterpret_cast<LPCSTR>(window_class),
                           "yabridge plugin", WS_POPUP, CW_USEDEFAULT,
                           CW_USEDEFAULT, 256, 256, nullptr, nullptr,
                           GetModuleHandle(nullptr), nullptr),
            &DestroyWindow);

    return win32_handle->get();
}

void Editor::close() {
    // RAII does the rest for us
    win32_handle = std::nullopt;

    // TODO: Do we need to do something on the X11 side or does the host do
    //       everything for us?
}

// TODO: I feel like this should only have to be done once
bool Editor::resize(const VstRect& new_size) {
    if (!win32_handle.has_value()) {
        return false;
    }

    SetWindowPos(win32_handle->get(), HWND_TOP, new_size.left, new_size.top,
                 new_size.right - new_size.left, new_size.bottom - new_size.top,
                 0);

    return true;
}

// TODO: Below function shouldn't be needed
bool Editor::update() {
    if (!win32_handle.has_value()) {
        return false;
    }

    // TODO: Doing this manually should not be needed
    UpdateWindow(win32_handle->get());

    // TODO: This should also be done somewhere else
    // Pump events since the Win32 API won't do it for us
    MSG msg;
    while (PeekMessage(&msg, win32_handle->get(), 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return true;
}

bool Editor::embed_into(const size_t parent_window_handle) {
    if (!win32_handle.has_value()) {
        return false;
    }

    const size_t child_window_handle = get_x11_handle().value();

    // TODO: Reparenting works, but the Wine window is not updating so that
    //       might cause it to look like it doesn't work
    xcb_reparent_window(x11_connection.get(), child_window_handle,
                        parent_window_handle, 0, 0);

    // TODO: Is this needed?
    const int parent_events = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                              XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                              XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    const int child_events =
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
        XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_STRUCTURE_NOTIFY |
        XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_change_window_attributes(x11_connection.get(), parent_window_handle,
                                 XCB_CW_EVENT_MASK, &parent_events);
    xcb_change_window_attributes(x11_connection.get(), child_window_handle,
                                 XCB_CW_EVENT_MASK, &child_events);

    // TODO: Is this map needed?
    xcb_map_window(x11_connection.get(), child_window_handle);
    xcb_flush(x11_connection.get());

    ShowWindow(win32_handle->get(), SW_SHOWNORMAL);
    // TODO: We should just immediatly resize the window to the right size
    //       isntead
    UpdateWindow(win32_handle->get());

    return true;
}

std::optional<size_t> Editor::get_x11_handle() {
    if (!win32_handle.has_value()) {
        return std::nullopt;
    }

    return reinterpret_cast<size_t>(
        GetProp(win32_handle.value().get(), "__wine_x11_whole_window"));
}

ATOM register_window_class(std::string window_class_name) {
    WNDCLASSEX window_class{};

    window_class.cbSize = sizeof(WNDCLASSEX);
    window_class.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    // TODO: Probably do something here to handle resizes
    window_class.lpfnWndProc = DefWindowProc;
    window_class.hInstance = GetModuleHandle(nullptr);
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.hbrBackground = CreateHatchBrush(HS_CROSS, RGB(255, 0, 255));
    window_class.lpszClassName = window_class_name.c_str();

    return RegisterClassEx(&window_class);
}
