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
#pragma pop_macro("_WIN32")

#include "../common/configuration.h"
#include "../common/logging/common.h"
#include "utils.h"
#include "xdnd-proxy.h"

/**
 * The maximum number of Win32 messages to handle per message loop. This is
 * needed because otherwise some plugins can run into an infinite loop. I've
 * observed this with:
 *
 * - Waves plugins
 * - Melda plugins when having multiple editor windows open within a single
 *   plugin group
 */
constexpr int max_win32_messages [[maybe_unused]] = 20;

/**
 * The most significant bit in an X11 event's response type is used to indicate
 * the event source.
 */
constexpr uint8_t xcb_event_type_mask = 0b0111'1111;

/**
 * The name of the X11 property that indicates whether a window supports
 * drag-and-drop. If the `editor_force_dnd` option is enabled we'll remove this
 * property from all of `parent_window`'s ancestors to work around a bug in
 * REAPER.
 */
constexpr char xdnd_aware_property_name[] = "XdndAware";

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
     * Post a `WM_CLOSE` message to the `handle`'s message queue as described
     * above.
     */
    ~DeferredWin32Window() noexcept;

    const HWND handle;

   private:
    MainContext& main_context;
    std::shared_ptr<xcb_connection_t> x11_connection;
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
 * to prevent the host from directly using the size of `wine_window`, which has
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
 *
 * TODO: Check if we can remove the double embed option after implementing this
 * TODO: Update architecture document
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
     * @see win32_window
     */
    Editor(
        MainContext& main_context,
        const Configuration& config,
        Logger& logger,
        const size_t parent_window_handle,
        std::optional<fu2::unique_function<void()>> timer_proc = std::nullopt);

    /**
     * Resize the `wrapper_window` to this new size. We need to manually call
     * this whenever the plugin requests a resize, or when the host resizes the
     * window (using the plugin API). Before yabridge 3.5.0 this was implicit.
     */
    void resize(uint16_t width, uint16_t height) noexcept;

    /**
     * Handle X11 events sent to the window our editor is embedded in.
     */
    void handle_x11_events() noexcept;

    /**
     * Get the Win32 window handle so it can be passed to an `effEditOpen()`
     * call. This will return the child window's handle if double editor
     * embedding is enabled.
     */
    HWND get_win32_handle() const noexcept;

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
     */
    void fix_local_coordinates() const;

    /**
     * Steal or release keyboard focus. This is done whenever the user clicks on
     * the window since we don't have a way to detect whether the client window
     * is calling `SetFocus()`. See the comment inside of this function for more
     * details on when this is used.
     *
     * @param grab Whether to grab input focus (if `true`) or to give back input
     *   focus to `host_window` (if `false`).
     */
    void set_input_focus(bool grab) const;

    /**
     * Run the timer proc function passed to the constructor, if one was passed.
     *
     * @see idle_timer
     * @see idle_timer_proc
     */
    void maybe_run_timer_proc();

    /**
     * Whether to use XEmbed instead of yabridge's normal window embedded. Wine
     * with XEmbed tends to cause rendering issues, so it's disabled by default.
     */
    const bool use_xembed;

   private:
    /**
     * Returns `true` if the currently active window (as per
     * `_NET_ACTIVE_WINDOW`) contains `wine_window`. If the window manager does
     * not support this hint, this will always return false.
     *
     * @see Editor::supports_ewmh_active_window
     */
    bool is_wine_window_active() const;

    /**
     * After `parent_window` gets reparented, we may need to redetect which
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
     * Start the XEmbed procedure when `use_xembed` is enabled. This should be
     * rerun whenever visibility changes.
     */
    void do_xembed() const;

    /**
     * The logger instance we will print debug tracing information to.
     */
    Logger& logger;

    /**
     * Every editor window gets its own X11 connection.
     */
    std::shared_ptr<xcb_connection_t> x11_connection;

    /**
     * A handle for our Wine->X11 drag-and-drop proxy. We only have one of these
     * per process, and it gets freed again when the last handle gets dropped.
     */
    WineXdndProxy::Handle dnd_proxy_handle;

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
     * The handle for the window created through Wine that the plugin uses to
     * embed itself in.
     */
    DeferredWin32Window win32_window;

    /**
     * A child window embedded inside of `win32_window`. This is only used if
     * the `editor_double_embed` option is enabled. It can be used as a
     * workaround for plugins that rely on their parent window's screen
     * coordinates instead of their own (see the 'Editor hosting modes' section
     * of the readme for more details). The plugin should then embed itself
     * within this child window.
     */
    std::optional<DeferredWin32Window> win32_child_window;

    /**
     * A timer we'll use to periodically run `idle_timer_proc`, if set. Thisi is
     * only needed for VST2 plugins, as they expected the host to periodically
     * send an idle event. We used to just pass through the calls from the host
     * before yabridge 3.x, but doing it ourselves here makes things m much more
     * manageable and we'd still need a timer anyways for when the GUI is
     * blocked.
     */
    Win32Timer idle_timer;

    /**
     * A function to call when the Win32 timer procs. This is used to
     * periodically call `effEditIdle()` for VST2 plugins even if the GUI is
     * being blocked.
     */
    std::optional<fu2::unique_function<void()>> idle_timer_proc;

    /**
     * The atom corresponding to `WM_STATE`.
     */
    xcb_atom_t xcb_wm_state_property;

    /**
     * The window handle of the editor window created by the DAW.
     */
    const xcb_window_t parent_window;
    /**
     * A window that sits between `parent_window` and `wine_window`. The entire
     * purpose of this is to prevent the host from responding to the
     * `ConfigureNotify` events we send to `wine_window` when the host
     * subscribes to `SubStructureNotify` events on `parent_window`.
     */
    X11Window wrapper_window;
    /**
     * The X11 window handle of the window belonging to `win32_window`.
     */
    const xcb_window_t wine_window;
    /**
     * The toplevel X11 window `parent_window` is contained in, or
     * `parent_window` if the host doesn't do any fancy window embedding. We'll
     * find this by looking for the topmost ancestor window of `parent_window`
     * that has `WM_STATE` set. This is similar to how `xprop` and `xwininfo`
     * select windows. In most cases this is going to be the same as
     * `parent_window`, but some DAWs (such as REAPER) embed `parent_window`
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
    xcb_window_t host_window;

    /**
     * The atom corresponding to `_NET_ACTIVE_WINDOW`.
     */
    xcb_atom_t active_window_property;
    /**
     * Whether the root window supports the `_NET_ACTIVE_WINDOW` hint. We'll
     * check this once and then cache the results in
     * `supports_ewmh_active_window()`.
     */
    mutable std::optional<bool> supports_ewmh_active_window_cache;

    /**
     * The atom corresponding to `_XEMBED`.
     */
    xcb_atom_t xcb_xembed_message;
};
