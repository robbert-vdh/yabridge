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

// Use the native version of xcb
#pragma push_macro("_WIN32")
#undef _WIN32
#include <xcb/xcb.h>
#pragma pop_macro("_WIN32")

#define NOMINMAX
#define NOSERVICE
#define NOMCX
#define NOIMM
#define WIN32_LEAN_AND_MEAN
#include <vestige/aeffectx.h>
#include <windows.h>

#include <memory>
#include <optional>
#include <string>

/**
 * A basic RAII wrapper around the Win32 window class system, for use in the
 * Editor class below.
 */
class WindowClass {
   public:
    WindowClass(std::string name);
    ~WindowClass();

    /**
     * The Win32 window class registered for the windows window.
     */
    const ATOM atom;
};

/**
 * A wrapper around the win32 windowing API to create and destroy editor
 * windows. We can embed this window into the window provided by the host, and a
 * VST plugin can then later embed itself in the window create here.
 *
 * This was originally implemented using XEmbed. Even though that sounded like
 * the right thing to do, there were a few small issues with Wine's XEmbed
 * implementation. The most important of which is that resizing GUIs sometimes
 * works fine, but often fails to expand the embedded window's client area
 * leaving part of the window inaccessible. There are also some a small number
 * of plugins (such as Serum) that have rendering issues when using XEmbed but
 * otherwise draw fine when running standalone or when just reparenting the
 * window without using XEmbed. If anyone knows how to work around these two
 * issues, please let me know and I'll switch to using XEmbed again.
 *
 * This workaround was inspired by LinVST.
 */
class Editor {
   public:
    /**
     * Open a window, embed it into the DAW's parent window and create a handle
     * to the new Win32 window that can be used by the hosted VST plugin.
     *
     * @param window_class_name The name for the window class for editor
     *   windows.
     * @param effect The plugin this window is being created for. Used to send
     *   `effEditIdle` messages on a timer.
     * @param parent_window_handle The X11 window handle passed by the VST host
     *   for the editor to embed itself into.
     *
     * @see win32_handle
     */
    Editor(const std::string& window_class_name,
           AEffect* effect,
           const size_t parent_window_handle);

    ~Editor();

    /**
     * Send a single `effEditIdle` event to the plugin to allow it to update its
     * GUI state. This is called periodically from a timer while the GUI is
     * being blocked, and also called explicitly by the host on a timer.
     */
    void send_idle_event();

    /**
     * Pump messages from the editor GUI's event loop until all events are
     * process. Must be run from the same thread the GUI was created in because
     * of Win32 limitations.
     */
    void handle_events();

   private:
    /**
     * The Win32 window class registered for the windows window.
     */
    WindowClass window_class;

   public:
    /**
     * The handle for the window created through Wine that the plugin uses to
     * embed itself in.
     */
    std::unique_ptr<std::remove_pointer_t<HWND>, decltype(&DestroyWindow)>
        win32_handle;

   private:
    /**
     * The window handle of the editor window created by the DAW.
     */
    xcb_window_t parent_window;
    /**
     * The X11 window handle of the window belonging to  `win32_handle`.
     */
    xcb_window_t child_window;

    /**
     *Needed to handle idle updates through a timer
     */
    AEffect* plugin;

    /**
     * A pointer to the currently active window. Will be a null pointer if no
     * window is active.
     */
    std::unique_ptr<xcb_connection_t, decltype(&xcb_disconnect)> x11_connection;
};
