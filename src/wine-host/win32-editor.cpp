#include "win32-editor.h"

ATOM register_window_class(std::string window_class_name);

Win32Editor::Win32Editor(std::string window_class_name)
    : window_class(register_window_class(window_class_name)) {}

HWND Win32Editor::open() {
    window_handle =
        std::unique_ptr<std::remove_pointer_t<HWND>, decltype(&DestroyWindow)>(
            CreateWindowEx(WS_EX_TOOLWINDOW,
                           reinterpret_cast<LPCSTR>(window_class),
                           "yabridge plugin", 0, 0, 0, 256, 256, nullptr,
                           nullptr, GetModuleHandle(nullptr), nullptr),
            &DestroyWindow);

    return window_handle->get();
}

void Win32Editor::close() {
    // RAII does the rest for us
    window_handle = std::nullopt;
}

std::optional<intptr_t> Win32Editor::get_x11_handle() {
    if (!window_handle.has_value()) {
        return std::nullopt;
    }

    return reinterpret_cast<intptr_t>(
        GetProp(window_handle.value().get(), "__wine_x11_whole_window"));
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
