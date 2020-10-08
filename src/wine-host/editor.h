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

#include "../common/configuration.h"
#include "utils.h"

/**
 * The maximum number of Win32 messages to handle per message loop. This is
 * needed because otherwise some plugins can run into an infinite loop. I've
 * observed this with:
 *
 * - Waves plugins
 * - Melda plugins when having multiple editor windows open within a single
 *   plugin group
 */
constexpr int max_win32_messages = 20;

/**
 * Used to store the maximum width and height of a screen.
 */
struct Size {
    uint16_t width;
    uint16_t height;
};

/**
 * A basic RAII wrapper around the Win32 window class system, for use in the
 * Editor class below.
 */
class WindowClass {
   public:
    explicit WindowClass(const std::string& name);
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
     * @param config This instance's configuration, used to enable alternative
     *   editor behaviours.
     * @param window_class_name The name for the window class for editor
     *   windows.
     * @param parent_window_handle The X11 window handle passed by the VST host
     *   for the editor to embed itself into.
     * @param effect The plugin this window is being created for. Used to send
     *   `effEditIdle` messages on a timer.
     *
     * @see win32_handle
     */
    Editor(const Configuration& config,
           const std::string& window_class_name,
           const size_t parent_window_handle,
           AEffect* effect);

    ~Editor();

    /**
     * Get the Win32 window handle so it can be passed to an `effEditOpen()`
     * call. This will return the child window's handle if double editor
     * embedding is enabled.
     */
    HWND get_win32_handle();

    /**
     * Send a single `effEditIdle` event to the plugin to allow it to update its
     * GUI state. This is called periodically from a timer while the GUI is
     * being blocked, and also called explicitly by the host on a timer.
     */
    void send_idle_event();

    /**
     * Pump messages from the editor loop loop until all events are process.
     * Must be run from the same thread the GUI was created in because of Win32
     * limitations.
     */
    void handle_win32_events() const;

    /**
     * Handle X11 events sent to the window our editor is embedded in.
     */
    void handle_x11_events() const;

    /**
     * Lie to the Wine window about its coordinates on the screen for
     * reparenting without using XEmbed. See the comment at the top of the
     * implementation on why this is needed.
     */
    void fix_local_coordinates() const;

    /**
     * Steal or release keyboard focus. This is done whenever the user clicks on
     * the window since we don't have a way to detect whether the client window
     * is calling `SetFocus()`. See the comment inside of this function for more
     * details on when this is used.
     *
     * @param grab Whether to grab input focus (if `true`) or to give back input
     *   focus to `topmost_window` (if `false`).
     */
    void set_input_focus(bool grab) const;

   private:
    /**
     * Returns `true` if the currently active window (as per
     * `_NET_ACTIVE_WINDOW`) contains `wine_window`.
     */
    bool is_wine_window_active() const;

    /**
     * A pointer to the currently active window. Will be a null pointer if no
     * window is active.
     */
    std::unique_ptr<xcb_connection_t, decltype(&xcb_disconnect)> x11_connection;

    /**
     * The Wine window's client area, or the maximum size of that window. This
     * will be set to a size that's large enough to be able to enter full screen
     * on a single display. This is more of a theoretical maximum size, as the
     * plugin will only use a portion of this window to draw to. Because we're
     * not changing the size of the Wine window and simply letting the user or
     * the host resize the X11 parent window it's been embedded in instead,
     * resizing will feel smooth and native.
     */
    const Size client_area;

    /**
     * The Win32 window class registered for the windows window.
     */
    const WindowClass window_class;

    /**
     * The handle for the window created through Wine that the plugin uses to
     * embed itself in.
     */
    std::unique_ptr<std::remove_pointer_t<HWND>, decltype(&DestroyWindow)>
        win32_handle;

    /**
     * A child window embedded inside of `win32_handle`. This is only used if
     * the `editor_double_embed` option is enabled. It can be used as a
     * workaround for plugins that rely on their parent window's screen
     * coordinates instead of their own (see the 'Editor hosting modes' section
     * of the readme for more details). The plugin should then embed itself
     * within this child window.
     */
    std::optional<
        std::unique_ptr<std::remove_pointer_t<HWND>, decltype(&DestroyWindow)>>
        win32_child_handle;

    /**
     * The Win32 API will block the `DispatchMessage` call when opening e.g. a
     * dropdown, but it will still allow timers to be run so the GUI can still
     * update in the background. Because of this we send `effEditIdle` to the
     * plugin on a timer. The refresh rate is purposely fairly low since the
     * host will call `effEditIdle()` explicitely when the plugin is not busy.
     */
    Win32Timer idle_timer;

    /**
     * The window handle of the editor window created by the DAW.
     */
    const xcb_window_t parent_window;
    /**
     * The X11 window handle of the window belonging to  `win32_handle`.
     */
    const xcb_window_t wine_window;
    /**
     * The X11 window that's at the top of the window tree starting from
     * `parent_window`, i.e. a direct child of the root window. In most cases
     * this is going to be the same as `parent_window`, but some DAWs (such as
     * REAPER) embed `parent_window` into another window. We have to listen for
     * configuration changes on this topmost window to know when the window is
     * being dragged around.
     */
    const xcb_window_t topmost_window;

    /**
     *Needed to handle idle updates through a timer
     */
    AEffect* plugin;

    /**
     * The atom corresponding to `_NET_ACTIVE_WINDOW`.
     */
    xcb_atom_t active_window_property;
};
