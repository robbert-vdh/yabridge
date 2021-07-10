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

#include <atomic>
#include <iostream>

#include "editor.h"

// As defined in `editor.cpp`
#define THROW_X11_ERROR(error)                                          \
    do {                                                                \
        if (error) {                                                    \
            free(error);                                                \
            throw std::runtime_error("X11 error in " +                  \
                                     std::string(__PRETTY_FUNCTION__)); \
        }                                                               \
    } while (0)

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

void WineXdndProxy::begin_xdnd(
    const boost::container::small_vector_base<std::string>& file_paths,
    HWND tracker_window) {
    // When XDND starts, we need to start listening for mouse events so we can
    // react when the mouse cursor hovers over a target that supports XDND. The
    // actual file contents will be transferred over X11 selections. See the
    // spec for a description of the entire process:
    // https://www.freedesktop.org/wiki/Specifications/XDND/#atomsandproperties
    xcb_set_selection_owner(x11_connection.get(), proxy_window.window,
                            xcb_xdnd_selection, XCB_CURRENT_TIME);
    xcb_flush(x11_connection.get());

    // Normally at this point you would grab the mouse pointer and track what
    // windows it's moving over. Wine is already doing this, so as a hacky
    // workaround we will instead just periodically poll the pointer position in
    // `WineXdndProxy::handle_x11_events()`, and we'll consider the
    // disappearance of `tracker_window` to indicate that the drag-and-drop has
    // either been cancelled or it has succeeded.
    dragged_file_paths.assign(file_paths.begin(), file_paths.end());
    this->tracker_window = tracker_window;

    // Because Wine is blocking the GUI thread, we need to do our XDND polling
    // from another thread. Luckily the X11 API is thread safe.
    xdnd_handler = Win32Thread([&]() { run_xdnd_loop(); });
}

void WineXdndProxy::end_xdnd() {
    xcb_set_selection_owner(x11_connection.get(), XCB_NONE, xcb_xdnd_selection,
                            XCB_CURRENT_TIME);
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

void WineXdndProxy::run_xdnd_loop() {
    const xcb_window_t root_window =
        xcb_setup_roots_iterator(xcb_get_setup(x11_connection.get()))
            .data->root;
    const HWND windows_desktop_window = GetDesktopWindow();

    // We cannot just grab the pointer because Wine is already doing that, and
    // it's also blocking the GUI thread. So instead we will periodically poll
    // the mouse cursor position, and we will consider the disappearance of
    // `tracker_window` to mean that the drag-and-drop operation has ended.
    uint16_t last_pointer_x = ~0;
    uint16_t last_pointer_y = ~0;
    while (IsWindow(tracker_window)) {
        usleep(1000);

        std::unique_ptr<xcb_generic_event_t> generic_event;
        while (generic_event.reset(xcb_poll_for_event(x11_connection.get())),
               generic_event != nullptr) {
            const uint8_t event_type =
                generic_event->response_type & xcb_event_type_mask;
            switch (event_type) {
                // TODO: Handle ConvertSelection
                // TODO: Handle client messages
            }
        }

        xcb_generic_error_t* error = nullptr;
        const xcb_query_pointer_cookie_t query_pointer_cookie =
            xcb_query_pointer(x11_connection.get(), root_window);
        const std::unique_ptr<xcb_query_pointer_reply_t> query_pointer_reply(
            xcb_query_pointer_reply(x11_connection.get(), query_pointer_cookie,
                                    &error));
        if (error) {
            free(error);
            continue;
        }
        if (query_pointer_reply->root_x == last_pointer_x &&
            query_pointer_reply->root_y == last_pointer_y) {
            continue;
        }

        // We want to ignore all Wine windows (within this prefix), since Wine
        // will be able to handle the drag-and-drop better than we can
        POINT windows_pointer_pos;
        GetCursorPos(&windows_pointer_pos);
        if (HWND windows_window = WindowFromPoint(windows_pointer_pos);
            windows_window && windows_window != windows_desktop_window) {
            continue;
        }

        // TODO: Fetch the window under the mouse cursor, send messages to it
        //       according to the XDND protocol

        last_pointer_x = query_pointer_reply->root_x;
        last_pointer_y = query_pointer_reply->root_y;
    }

    // TODO: Check if the escape key is pressed to allow cancelling the drop,
    //       and either send the drop or leave message to the window that was
    //       under the pointer

    end_xdnd();
}

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

    // This will contain the normal, Unix-style paths to the files
    boost::container::small_vector<std::string, 4> dragged_files;
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

                        dragged_files.emplace_back(
                            wine_get_unix_file_name(file_name.data()));
                    }

                    GlobalUnlock(storage.hGlobal);
                } break;
                case TYMED_FILE: {
                    dragged_files.emplace_back(
                        wine_get_unix_file_name(storage.lpszFileName));
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

#undef THROW_X11_ERROR
