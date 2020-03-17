#include "editor.h"

ATOM register_window_class(std::string window_class_name);

Editor::Editor(std::string window_class_name)
    : window_class(register_window_class(window_class_name)),
      x11_connection(xcb_connect(nullptr, nullptr), &xcb_disconnect) {}

HWND Editor::open() {
    win32_handle =
        std::unique_ptr<std::remove_pointer_t<HWND>, decltype(&DestroyWindow)>(
            CreateWindowEx(WS_EX_TOOLWINDOW,
                           reinterpret_cast<LPCSTR>(window_class),
                           "yabridge plugin", 0, 0, 0, 256, 256, nullptr,
                           nullptr, GetModuleHandle(nullptr), nullptr),
            &DestroyWindow);

    return win32_handle->get();
}

void Editor::close() {
    // RAII does the rest for us
    win32_handle = std::nullopt;

    // TODO: Do we need to do something on the X11 side or does the host do
    //       everything for us?
}

#include <iostream>

bool Editor::embed_into(const size_t parent_window_handle) {
    if (!win32_handle.has_value()) {
        return false;
    }

    // TODO: Swap the order if that works once everything else works so you
    //       don't get to see the Wine window
    ShowWindow(win32_handle->get(), SW_SHOW);
    UpdateWindow(win32_handle->get());

    const size_t child_window_handle = get_x11_handle().value();

    // TODO: Reparenting works, but the Wine window is not updating so that
    //       might cause it to look like it doesn't work
    xcb_reparent_window(x11_connection.get(), child_window_handle,
                        parent_window_handle, 0, 0);
    // TODO: Is this map needed?
    xcb_map_window(x11_connection.get(), child_window_handle);
    xcb_flush(x11_connection.get());

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
