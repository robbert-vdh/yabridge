#include "editor.h"

#include <xcb/xcb_ewmh.h>

constexpr char xembed_proeprty[] = "_XEMBED";
constexpr char xembed_info_proeprty[] = "_XEMBED_INFO";

// Constants from the XEmbed spec
constexpr uint32_t xembed_protocol_version = 0;

constexpr uint32_t xembed_embedded_notify_msg = 0;
constexpr uint32_t xembed_window_activate_msg = 1;
constexpr uint32_t xembed_focus_in_msg = 4;

constexpr uint32_t xembed_focus_first = 1;

ATOM register_window_class(std::string window_class_name);

Editor::Editor(std::string window_class_name)
    : window_class(register_window_class(window_class_name)),
      x11_connection(xcb_connect(nullptr, nullptr), &xcb_disconnect) {
    // We need a bunch of property atoms for the XEmbed protocol
    xcb_generic_error_t* err;

    const auto xembed_cookie = xcb_intern_atom(
        x11_connection.get(), 0, strlen(xembed_proeprty), xembed_proeprty);
    const auto xembed_info_cookie =
        xcb_intern_atom(x11_connection.get(), 0, strlen(xembed_info_proeprty),
                        xembed_info_proeprty);

    xcb_xembed =
        xcb_intern_atom_reply(x11_connection.get(), xembed_cookie, &err)->atom;
    xcb_xembed_info =
        xcb_intern_atom_reply(x11_connection.get(), xembed_info_cookie, &err)
            ->atom;
}

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
    // RAII will destroy the window for us
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

bool Editor::embed_into(const size_t parent_window_handle) {
    if (!win32_handle.has_value()) {
        return false;
    }

    // TODO: Right now the child's child windows are not anchored to the window
    //       and do not receive keyboard focus. THis affects things like
    //       dropdowns.

    // This follows the embedding procedure specified in the XEmbed sped:
    // https://specifications.freedesktop.org/xembed-spec/xembed-spec-latest.html
    // under 'Embedding life cycle
    // Sadly there's doesn't seem to be any implementation of this available as
    // a library
    const size_t child_window_handle = get_x11_handle().value();

    xcb_reparent_window(x11_connection.get(), child_window_handle,
                        parent_window_handle, 0, 0);

    // Honestly, I'm not sure if all of this XEmbed stuff is even doing anything

    // This tells the WM that the parent window embedding and mapping/uumapping
    // a child window. Requires the PROPERTY_NOTIFY event.
    std::array<int, 2> xembed_info_values{xembed_protocol_version, 1};
    xcb_change_property(x11_connection.get(), XCB_PROP_MODE_REPLACE,
                        child_window_handle, xcb_xembed_info, xcb_xembed_info,
                        32, 2, xembed_info_values.data());

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

    // Tell the window from Wine it's embedded into the window provided by the
    // host
    send_xembed_event(child_window_handle, xembed_embedded_notify_msg, 0,
                      parent_window_handle, xembed_protocol_version);

    send_xembed_event(child_window_handle, xembed_focus_in_msg,
                      xembed_focus_first, 0, 0);
    send_xembed_event(child_window_handle, xembed_window_activate_msg, 0, 0, 0);

    xcb_map_window(x11_connection.get(), child_window_handle);
    xcb_flush(x11_connection.get());

    ShowWindow(win32_handle->get(), SW_SHOW);

    return true;
}

void Editor::handle_events() {
    // Process any remaining events, otherwise we won't be able to interact with
    // the window
    if (win32_handle.has_value()) {
        MSG msg;
        while (PeekMessage(&msg, win32_handle->get(), 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

std::optional<size_t> Editor::get_x11_handle() {
    if (!win32_handle.has_value()) {
        return std::nullopt;
    }

    return reinterpret_cast<size_t>(
        GetProp(win32_handle.value().get(), "__wine_x11_whole_window"));
}

void Editor::send_xembed_event(const xcb_window_t& window,
                               const uint32_t message,
                               const uint32_t detail,
                               const uint32_t data1,
                               const uint32_t data2) {
    const std::array<uint32_t, 5> event_data{XCB_TIME_CURRENT_TIME, message,
                                             detail, data1, data2};

    xcb_ewmh_send_client_message(x11_connection.get(), window, window,
                                 xcb_xembed, event_data.size(),
                                 event_data.data());
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
