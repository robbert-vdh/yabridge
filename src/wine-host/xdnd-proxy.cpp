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

// FIXME: Remove
#include <iostream>

/**
 * The window class name Wine uses for its `DoDragDrop()` tracker window.
 *
 * https://github.com/wine-mirror/wine/blob/d10887b8f56792ebcca717ccc28a289f7bcaf107/dlls/ole32/ole2.c#L101-L104
 */
static constexpr char OLEDD_DRAGTRACKERCLASS[] = "WineDragDropTracker32";

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

    std::array<FORMATETC, 16> supported_formats;
    unsigned int num_formats = 0;
    enumerator->Next(supported_formats.size(), supported_formats.data(),
                     &num_formats);
    enumerator->Release();
    for (unsigned int format_idx = 0; format_idx < num_formats; format_idx++) {
        STGMEDIUM storage{};
        if (HRESULT result = tracker_info->dataObject->GetData(
                &supported_formats[format_idx], &storage);
            result == S_OK) {
            switch (storage.tymed) {
                case TYMED_HGLOBAL: {
                    auto drop = static_cast<HDROP>(GlobalLock(storage.hGlobal));

                    std::array<WCHAR, 1024> file_name{0};
                    const uint32_t num_files = DragQueryFileW(
                        drop, 0xFFFFFFFF, file_name.data(), file_name.size());

                    std::cerr << "Plugin wanted to drag-and-drop " << num_files
                              << " file(s):" << std::endl;
                    for (uint32_t file_idx = 0; file_idx < num_files;
                         file_idx++) {
                        file_name[0] = 0;
                        DragQueryFileW(drop, file_idx, file_name.data(),
                                       file_name.size());

                        std::cerr << "- "
                                  << wine_get_unix_file_name(file_name.data())
                                  << std::endl;
                    }

                    GlobalUnlock(GlobalLock(storage.hGlobal));
                } break;
                case TYMED_FILE: {
                    std::cerr << "Plugin wanted to drag-and-drop '"
                              << wine_get_unix_file_name(storage.lpszFileName)
                              << "'" << std::endl;
                } break;
                default: {
                    std::cerr << "Unknown format " << storage.tymed
                              << std::endl;
                } break;
            }

            if (storage.pUnkForRelease) {
                storage.pUnkForRelease->Release();
            }
        }
    }
}

WineXdndProxy::WineXdndProxy()
    : hook_handle(
          SetWinEventHook(EVENT_OBJECT_CREATE,
                          EVENT_OBJECT_CREATE,
                          nullptr,
                          dnd_winevent_callback,
                          0,
                          0,
                          WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS),
          UnhookWinEvent) {}

WineXdndProxy& WineXdndProxy::init_proxy() {
    static std::unique_ptr<WineXdndProxy> instance;
    if (!instance) {
        // Protected constructors, hooray!
        instance.reset(new WineXdndProxy{});
    }

    return *instance;
}
