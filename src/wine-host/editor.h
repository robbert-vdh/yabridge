// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2024 Robbert van der Helm
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

#pragma once

#include <memory>
#include <optional>
#include <string>

#include <windows.h>
#include <function2/function2.hpp>

// Use the native version of xcb
#pragma push_macro("_WIN32")
#undef _WIN32
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#pragma pop_macro("_WIN32")

#include "../common/configuration.h"
#include "../common/logging/common.h"
#include "utils.h"
#include "xdnd-proxy.h"

/**
 * The most significant bit in an X11 event's response type is used to indicate
 * the event source.
 */
constexpr uint8_t xcb_event_type_mask = 0b0111'1111;

/**
 * The name of the X11 property that indicates whether a window supports
 * drag-and-drop. If the `editor_force_dnd` option is enabled we'll remove this
 * property from all of `parent_window_`'s ancestors to work around a bug in
 * REAPER.
 */
constexpr char xdnd_aware_property_name[] = "XdndAware";

/**
 * The name of the Win32 window class we'll use for the Win32 window that the
 * plugin can embed itself in.
 */
constexpr char yabridge_window_class_name[] = "yabridge plugin";

/**
 * Get the atom with the specified name. May throw when
 * `xcb_intern_atom_reply()` returns an error. Returns `XCB_ATOM_NONE` when the
 * atom doesn't exist. We define this here because we'll also need to fetch a
 * whole bunch of atoms for the We define this here because we'll also need to
 * fetch a whole bunch of atoms for the XDND protocol in `xdnd-proxy.cpp`.
 */
xcb_atom_t get_atom_by_name(xcb_connection_t& x11_connection,
                            const char* atom_name);

/**
 * Check if the cursor is within a Wine window. We can of course only detect
 * Wine applications within the current prefix. This ignores the extended client
 * area of yabridge windows. (so it will consider other Wine windows to the
 * right or to the bottom of a yabridge plugin editor, but not the extended
 * client area itself)
 *
 * @param windows_pointer_pos The screen coordinates to use for the query. If
 *   this is left at a nullopt, we will simply use `GetCursorPos()`. Note that
 *   this value only updates once every 100 milliseconds.
 */
bool is_cursor_in_wine_window(
    std::optional<POINT> windows_pointer_pos = std::nullopt) noexcept;

/**
 * Used to store the maximum width and height of a screen.
 */
struct Size {
    uint16_t width;
    uint16_t height;
};

/**
 * A RAII wrapper around windows created using `CreateWindow()` that will post a
 * `WM_CLOSE` message to the window's message loop so it can clean itself up
 * later. Directly calling `DestroyWindow()` might hang for a second or two, so
 * deferring this increases responsiveness. We actually defer this even further
 * by calling this function a little while after the editor has closed to
 * prevent any potential delays.
 *
 * This is essentially an alternative around `std::unique_ptr` with a non-static
 * custom deleter.
 */
class DeferredWin32Window {
   public:
    /**
     * Manage a window so that it will be asynchronously closed when this object
     * gets dropped.
     *
     * @param main_context This application's main IO context running on the GUI
     *   thread.
     * @param x11_connection The X11 connection handle we're using for this
     *   editor.
     * @param window A `HWND` obtained through a call to `CreateWindowEx`
     */
    DeferredWin32Window(MainContext& main_context,
                        std::shared_ptr<xcb_connection_t> x11_connection,
                        HWND window) noexcept;

    /**
     * Post a `WM_CLOSE` message to the `handle_`'s message queue as described
     * above.
     */
    ~DeferredWin32Window() noexcept;

    const HWND handle_;

   private:
    MainContext& main_context_;
    std::shared_ptr<xcb_connection_t> x11_connection_;
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
 * This workaround was inspired by LinVst.
 *
 * As of yabridge 3.0 XEmbed is back as an option, but it's disabled by default
 * because of the issues mentioned above.
 *
 * In yabridge 3.5.0 we added another layer to the embedding structure. This is
 * to prevent the host from directly using the size of `wine_window_`, which has
 * a client area the size of the entire root window so the window can resized
 * and fullscreened at will. Some hosts, like Carla 2.3.1 (this didn't happen in
 * earlier versions), may directly resize their editor window depending on the
 * child window's size even without using XEmbed. To combat this, we need to
 * manually manage a window that sits in between the parent window and wine's
 * window. The embedding structure thus ends up looking like:
 *
 * ```
 * [host_window ->] parent_window -> wrapper_window -> wine_window
 * ```
 *
 * Where `host_window` and `parent_window` may be the same window (which will be
 * the case for most hosts), and `wine_window` is the X11 window backing the
 * window we created using `CreateWindowEx()`. We will need to manually resize
 * `wrapper_window` to match size changes coming from and going to the plugin
 * belonging to `wine_window`.
 */
class Editor {
   public:
    /**
     * Open a window, embed it into the DAW's parent window and create a handle
     * to the new Win32 window that can be used by the hosted VST plugin.
     *
     * @param main_context The application's main IO context running on the GUI
     *   thread. We use this to defer closing the window in
     *   `DestroyWindow::~DestroyWindow()`.
     * @param config This instance's configuration, used to enable alternative
     *   editor behaviours.
     * @param logger A logger instance created with
     *   `Logger::create_wine_stderr()`. We'll use this to print editor tracing
     *   information only when needed.
     * @param parent_window_handle The X11 window handle passed by the VST host
     *   for the editor to embed itself into.
     * @param timer_proc A function to run on a timer. This is used for VST2
     *   plugins to periodically call `effEditIdle` from the message loop
     *   thread, even when the GUI is blocked.
     *
     * @see win32_window_
     */
    Editor(
        MainContext& main_context,
        const Configuration& config,
        Logger& logger,
        const size_t parent_window_handle,
        std::optional<fu2::unique_function<void()>> timer_proc = std::nullopt);

    /**
     * Resize the `wrapper_window_` to this new size. We need to manually call
     * this whenever the plugin requests a resize, or when the host resizes the
     * window (using the plugin API). Before yabridge 3.5.0 this was implicit.
     */
    void resize(uint16_t width, uint16_t height);

    /**
     * Show the window, should be called after the plugin has embedded itself.
     * There's absolutely zero reason why this can't be done in the constructor
     * or in `do_xembed()`, but it needs to be. Thanks Waves.
     */
    void show() noexcept;

    /**
     * Handle X11 events sent to the window our editor is embedded in.
     */
    void handle_x11_events() noexcept;

    /**
     * Get the Win32 window handle so it can be passed to an `effEditOpen()`
     * call.
     */
    HWND win32_handle() const noexcept;

    /**
     * Returns `true` if the window manager supports the EWMH active window
     * protocol through the `_NET_ACTIVE_WINDOW` attribute. Some more
     * minimalistic window managers may not support this. In that case we'll
     * show a warning and fall back to a more hacky approach to grabbing input
     * focus. This involves checking whether the `_NET_ACTIVE_WINDOW` atom
     * exists and whether the property is set on the root window. The result is
     * cached in `supports_ewmh_active_window_cache`.
     */
    bool supports_ewmh_active_window() const;

    /**
     * Lie to the Wine window about its coordinates on the screen for
     * reparenting without using XEmbed. See the comment at the top of the
     * implementation on why this is needed.
     *
     * One of the events that trigger this is `ConfigureNotify` messages. Some
     * WMs may continuously send this message while dragging a window around. To
     * avoid flickering, the main `handle_x11_events()` function will wait to
     * call this function until the all mouse buttons have been released.
     */
    void fix_local_coordinates() const;

    /**
     * Steal or release keyboard focus. This is done whenever the user clicks on
     * the window since we don't have a way to detect whether the client window
     * is calling `SetFocus()`. See the comment inside of this function for more
     * details on when this is used.
     *
     * NOTE: There's a little bit of special behaviour in here. When the shift
     *       key is held while grabbing input focus, then we'll focus
     *       `wine_window_` directly instead of focussing `wrapper_window_`.
     *       This allows you to temporarily override the default focus grabbing
     *       behaviour, allowing you to use the space key in plugins GUIs in
     *       Bitwig and to enter text in Voxengo settings and license dialogs.
     *       This can also help with plugins that use popups but still rely on
     *       the parent window's keyboard events to come up to control those
     *       popups.
     *
     * @param grab Whether to grab input focus (if `true`) or to give back input
     *   focus to `host_window_` (if `false`).
     */
    void set_input_focus(bool grab) const;

    /**
     * Run the X11 event loop plus the timer proc function passed to the
     * constructor, if one was passed.
     *
     * @see idle_timer_
     * @see idle_timer_proc_
     */
    void run_timer_proc();

    /**
     * Get the editor's (or, the wrapper window's) current size.
     */
    inline Size size() const noexcept { return wrapper_window_size_; }

    /**
     * Whether to reposition `win32_window_` to (0, 0) every time the window
     * resizes. This can help with buggy plugins that use the (top level)
     * window's screen coordinates when drawing their GUI.
     */
    const bool use_coordinate_hack_;

    /**
     * Whether the `editor_force_dnd` workaround for REAPER should be activated.
     * See the implementation in `editor.cpp` for more details.
     */
    const bool use_force_dnd_;

    /**
     * Whether to use XEmbed instead of yabridge's normal window embedded. Wine
     * with XEmbed tends to cause rendering issues, so it's disabled by default.
     */
    const bool use_xembed_;

   private:
    /**
     * Get the X11 event mask containing the current keyboard modifiers. Because
     * we don't want to link with `xcb-xkb` and we also can't really use
     * key/motion events for this, we'll do this by querying the pointer
     * position instead. Will return a nullopt if that query fails.
     */
    std::optional<uint16_t> get_active_modifiers() const noexcept;

    /**
     * Get the current cursor position, in Win32 screen coordinates. This is
     * needed for our `LeaveNotify` handling because `GetCursorPos()` only
     * updates once every 100 ms. This takes the X11 mouse cursor position, and
     * then adds to that the difference between `wine_window_`'s X11 coordinates
     * and its Win32 coordinates. This is kind of a workaround for Wine's
     * X11drv's `root_to_virtual_screen()` function not being exposed.
     *
     * If we cannot obtain the X11 cursor position, then this returns a nullopt.
     */
    std::optional<POINT> get_current_pointer_position() const noexcept;

    /**
     * Checks whether any mouse button is held. Used to defer calling
     * `fix_local_coordinates()` when dragging windows around.
     */
    bool is_mouse_button_held() const;

    /**
     * Returns `true` if the currently active window (as per
     * `_NET_ACTIVE_WINDOW`) contains `wine_window_`. If the window manager does
     * not support this hint, this will always return false.
     *
     * @see Editor::supports_ewmh_active_window
     */
    bool is_wine_window_active() const;

    /**
     * After `parent_window_` gets reparented, we may need to redetect which
     * toplevel-ish window the host is using and adjust the events we're
     * subscribed to accordingly.
     */
    void redetect_host_window() noexcept;

    /**
     * Send an XEmbed message to a window. This does not include a flush. See
     * the spec for more information:
     *
     * https://specifications.freedesktop.org/xembed-spec/xembed-spec-latest.html#lifecycle
     */
    void send_xembed_message(xcb_window_t window,
                             uint32_t message,
                             uint32_t detail,
                             uint32_t data1,
                             uint32_t data2) const noexcept;

    /**
     * Reparent `child` to `new_parent`. This includes the flush.
     */
    void do_reparent(xcb_window_t child, xcb_window_t new_parent) const;

    /**
     * Start the XEmbed procedure when `use_xembed_` is enabled. This should be
     * rerun whenever visibility changes.
     */
    void do_xembed() const;

    /**
     * The logger instance we will print debug tracing information to.
     */
    Logger& logger_;

    /**
     * Every editor window gets its own X11 connection.
     */
    std::shared_ptr<xcb_connection_t> x11_connection_;

    /**
     * A handle for our Wine->X11 drag-and-drop proxy. We only have one of these
     * per process, and it gets freed again when the last handle gets dropped.
     */
    WineXdndProxy::Handle dnd_proxy_handle_;

    /**
     * The Wine window's client area, or the maximum size of that window. This
     * will be set to a size that's large enough to be able to enter full screen
     * on a single display. This is more of a theoretical maximum size, as the
     * plugin will only use a portion of this window to draw to. Because we're
     * not changing the size of the Wine window and only resize the wrapper
     * window it's been embedded in, resizing will feel smooth and native.
     */
    const Size client_area_;

    /**
     * The size of the wrapper window. We'll prevent CLAP resize requests when
     * the wrapper window is already at the correct size.
     */
    Size wrapper_window_size_;

    /**
     * Last received configurations for the host and parent windows.
     */
    xcb_configure_notify_event_t host_window_config_;
    xcb_configure_notify_event_t parent_window_config_;

    /**
     * The handle for the window created through Wine that the plugin uses to
     * embed itself in.
     */
    DeferredWin32Window win32_window_;

    /**
     * A timer we'll use to periodically run the X11 event loop plus
     * `idle_timer_proc_`, if that is set. We handle X11 events from within the
     * Win32 event loop because that allows us to still process those while the
     * GUI is blocked. Additionally for VST2 plugins we also need this
     * `idle_timer_proc_`, as they expected the host to periodically send an
     * idle event. We used to just pass through the calls from the host before
     * yabridge 3.x, but doing it ourselves here makes things m much more
     * manageable and we'd still need a timer anyways for when the GUI is
     * blocked.
     */
    Win32Timer idle_timer_;

    /**
     * A function to call when the Win32 timer procs. This is used to
     * periodically call `handle_x11_events()`, as well as `effEditIdle()` for
     * VST2 plugins even if the GUI is being blocked.
     */
    fu2::unique_function<void()> idle_timer_proc_;

    /**
     * The atom corresponding to `WM_STATE`.
     */
    xcb_atom_t xcb_wm_state_property_;

    /**
     * The window handle of the editor window created by the DAW.
     */
    const xcb_window_t parent_window_;
    /**
     * A window that sits between `parent_window_` and `wine_window_`. The
     * entire purpose of this is to prevent the host from responding to the
     * `ConfigureNotify` events we send to `wine_window_` when the host
     * subscribes to `SubStructureNotify` events on `parent_window_`.
     */
    X11Window wrapper_window_;
    /**
     * The X11 window handle of the window belonging to `win32_window_`.
     */
    const xcb_window_t wine_window_;
    /**
     * The toplevel X11 window `parent_window_` is contained in, or
     * `parent_window_` if the host doesn't do any fancy window embedding. We'll
     * find this by looking for the topmost ancestor window of `parent_window_`
     * that has `WM_STATE` set. This is similar to how `xprop` and `xwininfo`
     * select windows. In most cases this is going to be the same as
     * `parent_window_`, but some DAWs (such as REAPER) embed `parent_window_`
     * into another window. We have to listen for configuration changes on this
     * topmost window to know when the window is being dragged around, and when
     * returning keyboard focus to the host we'll focus this window.
     *
     * NOTE: When reopening a REAPER FX window that has previously been closed,
     *       REAPER will initialize the first plugin's editor first before
     *       opening the window. This means that the topmost FX window doesn't
     *       actually exist yet at that point, so we need to redetect this
     *       later.
     * NOTE: Taking the very topmost window is not an option, because for some
     *       reason REAPER will only process keyboard input for that window when
     *       the mouse is within the window.
     */
    xcb_window_t host_window_;

    /**
     * Used to delay calling `fix_local_coordinates()` when dragging windows
     * around with the mouse. Some WMs will continuously send `ConfigureNotify`
     * messages when dragging windows around, and the `fix_local_coordinates()`
     * function may cause the window to blink. This becomes a but jarring if it
     * happens 60 times per second while dragging windows around.
     */
    bool should_fix_local_coordinates_ = false;

    /**
     * The atom corresponding to `_NET_ACTIVE_WINDOW`.
     */
    xcb_atom_t active_window_property_;
    /**
     * Whether the root window supports the `_NET_ACTIVE_WINDOW` hint. We'll
     * check this once and then cache the results in
     * `supports_ewmh_active_window()`.
     */
    mutable std::optional<bool> supports_ewmh_active_window_cache_;

    /**
     * The atom corresponding to `_XEMBED`.
     */
    xcb_atom_t xcb_xembed_message_;
};
