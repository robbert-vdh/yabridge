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

#include "editor.h"

#include <iostream>

using namespace std::literals::chrono_literals;

/**
 * The Win32 timer ID we'll use to periodically call the VST2 `effeditidle`
 * function with. We have to do this on a timer because the function has to be
 * called from the GUI thread, and it should also be called while the Win32
 * event loop is being blocked (for instance when a plugin opens a dropdown
 * menu).
 */
constexpr size_t idle_timer_id = 1337;

/**
 * The most significant bit in an X11 event's response type is used to indicate
 * the event source.
 */
constexpr uint8_t event_type_mask = 0b0111'1111;

/**
 * The name of the X11 property on the root window used to denote the active
 * window in EWMH compliant window managers.
 */
constexpr char active_window_property_name[] = "_NET_ACTIVE_WINDOW";

/**
 * The name of the X11 property that indicates whether a window supports
 * drag-and-drop. If the `editor_force_dnd` option is enabled we'll remove this
 * property from `topmost_window` to work around a bug in REAPER.
 */
constexpr char x_dnd_aware_property_name[] = "XdndAware";

/**
 * Client message name for XEmbed messages. See
 * https://specifications.freedesktop.org/xembed-spec/xembed-spec-latest.html.
 */
constexpr char xembed_message_name[] = "_XEMBED";

// Constants from the XEmbed spec
constexpr uint32_t xembed_protocol_version = 0;

constexpr uint32_t xembed_embedded_notify_msg = 0;
constexpr uint32_t xembed_window_activate_msg = 1;
constexpr uint32_t xembed_focus_in_msg = 4;

constexpr uint32_t xembed_focus_first = 1;

// A catchable alternative to `assert()`. Normally all of our `assert(!error)`
// should never fail, except for when Ardour hides the editor window without
// closing the editor. In those case some of our X11 function calls may r turn
// errors. When this happens we want to be able to catch them in
// `handle_x11_events()`.
#define THROW_X11_ERROR(error)                                          \
    do {                                                                \
        if (error) {                                                    \
            free(error);                                                \
            throw std::runtime_error("X111 error in " +                 \
                                     std::string(__PRETTY_FUNCTION__)); \
        }                                                               \
    } while (0)

/**
 * Find the the ancestors for the given window. This returns a list of window
 * IDs that starts wit h`starting_at`, and then iteratively contains the parent
 * of the previous window in the list until we reach the root window. The
 * topmost window (i.e. the window that will show up in the user's window
 * manager) will be the last window in this list.
 *
 * @param x11_connection The X11 connection to use.
 * @param starting_at The window we want to know the ancestor windows of.
 *
 * @return A non-empty list containing `starting_at` and all of its ancestor
 * windows `starting_at`.
 */
std::vector<xcb_window_t> find_ancestor_windows(
    xcb_connection_t& x11_connection,
    xcb_window_t starting_at);
/**
 * Check whether `child` is a descendant of `parent` or the same window. Used
 * during focus checks to only grab focus when needed.
 *
 * @param x11_connection The X11 connection to use.
 * @param child The potential child window.
 * @param parent The potential parent window.
 *
 * @return Whether `child` is a descendant of or the same window as `parent.`
 */
bool is_child_window_or_same(xcb_connection_t& x11_connection,
                             xcb_window_t child,
                             xcb_window_t parent);
/**
 * Compute the size a window would have to be to be allowed to fullscreened on
 * any of the connected screens.
 */
Size get_maximum_screen_dimensions(xcb_connection_t& x11_connection) noexcept;
/**
 * Get the root window for the specified window. The returned root window will
 * depend on the screen the window is on.
 */
xcb_window_t get_root_window(xcb_connection_t& x11_connection,
                             xcb_window_t window);
/**
 * Return the X11 window handle for the window if it's currently open.
 */
xcb_window_t get_x11_handle(HWND win32_handle) noexcept;

/**
 * Return a handle to the window class used for all Win32 windows created by
 * yabridge.
 */
ATOM get_window_class() noexcept;

DeferredWindow::DeferredWindow(MainContext& main_context,
                               std::shared_ptr<xcb_connection_t> x11_connection,
                               HWND window) noexcept
    : handle(window),
      main_context(main_context),
      x11_connection(x11_connection) {}

DeferredWindow::~DeferredWindow() noexcept {
    // NOTE: For some rason, Wine will sometimes try to delete a window twice if
    //       the parent window no longer exists. I've only seen this cause
    //       issues with plugins that hang when their window is hidden, like the
    //       iZotope Rx plugins. In Renoise this would otherwise trigger an X11
    //       error every time you close such a plugin's editor, and in other
    //       DAWs I've also seen it happen from time to time.
    try {
        const xcb_window_t wine_window = get_x11_handle(handle);
        const xcb_window_t root_window =
            get_root_window(*x11_connection, wine_window);
        xcb_reparent_window(x11_connection.get(), wine_window, root_window, 0,
                            0);
    } catch (const std::runtime_error& error) {
        std::cerr << error.what() << std::endl;
    }

    // XXX: We are already deferring this closing by posting `WM_CLOSE` to the
    //      message loop instead of calling `DestroyWindow()` ourselves, but we
    //      can take it one step further. If we post this message directly then
    //      we might still get a delay, for instance if our event loop timer
    //      would tick exactly between `IPlugView::removed()` and
    //      `IPlugView::~IPlugView`. Delaying this seems to be a best of both
    //      worlds solution that works as expected in every host I've tested.
    try {
        std::shared_ptr<boost::asio::steady_timer> destroy_timer =
            std::make_shared<boost::asio::steady_timer>(main_context.context);
        destroy_timer->expires_after(1s);

        // Note that we capture a copy of `destroy_timer` here. This way we
        // don't have to manage the timer instance ourselves as it will just
        // clean itself up after this lambda gets called.
        destroy_timer->async_wait([destroy_timer, handle = this->handle,
                                   x11_connection = this->x11_connection](
                                      const boost::system::error_code& error) {
            if (error.failed()) {
                return;
            }

            // This is the flush for the reparent done above. We'll also do this
            // as late as possible to prevent the window from being drawn in the
            // meantime, as that would cause flickering.
            xcb_flush(x11_connection.get());

            // The actual destroying will happen as part of the Win32 message
            // loop
            PostMessage(handle, WM_CLOSE, 0, 0);
        });
    } catch (const std::bad_alloc&) {
        // If we can't allocate the timer, then we probably have bigger worries
        // than not cleaning up a window
    }
}

Editor::Editor(MainContext& main_context,
               const Configuration& config,
               const size_t parent_window_handle,
               std::optional<fu2::unique_function<void()>> timer_proc)
    : use_xembed(config.editor_xembed),
      x11_connection(xcb_connect(nullptr, nullptr), xcb_disconnect),
      client_area(get_maximum_screen_dimensions(*x11_connection)),
      // Create a window without any decoratiosn for easy embedding. The
      // combination of `WS_EX_TOOLWINDOW` and `WS_POPUP` causes the window to
      // be drawn without any decorations (making resizes behave as you'd
      // expect) and also causes mouse coordinates to be relative to the window
      // itself.
      win32_window(main_context,
                   x11_connection,
                   CreateWindowEx(WS_EX_TOOLWINDOW,
                                  reinterpret_cast<LPCSTR>(get_window_class()),
                                  "yabridge plugin",
                                  WS_POPUP,
                                  CW_USEDEFAULT,
                                  CW_USEDEFAULT,
                                  client_area.width,
                                  client_area.height,
                                  nullptr,
                                  nullptr,
                                  GetModuleHandle(nullptr),
                                  this)),
      // If `config.editor_double_embed` is set, then we'll also create a child
      // window in `win32_child_window`. If we do this before calling
      // `ShowWindow()` on `win32_window` we'll run into X11 errors.
      win32_child_window(std::nullopt),
      idle_timer(
          timer_proc
              ? Win32Timer(
                    win32_window.handle,
                    idle_timer_id,
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        config.event_loop_interval())
                        .count())
              : Win32Timer()),
      idle_timer_proc(std::move(timer_proc)),
      parent_window(parent_window_handle),
      wine_window(get_x11_handle(win32_window.handle)),
      topmost_window(
          find_ancestor_windows(*x11_connection, parent_window).back()) {
    xcb_generic_error_t* error;

    // Used for input focus grabbing to only grab focus when the window is
    // active.
    xcb_intern_atom_cookie_t atom_cookie = xcb_intern_atom(
        x11_connection.get(), true, strlen(active_window_property_name),
        active_window_property_name);
    xcb_intern_atom_reply_t* atom_reply =
        xcb_intern_atom_reply(x11_connection.get(), atom_cookie, &error);
    THROW_X11_ERROR(error);

    // In case the atom does not exist or the WM does not support this hint,
    // we'll print a warning and fall back to grabbing focus when the user
    // clicks on the window (which should trigger a `WM_PARENTNOTIFY`).
    active_window_property = atom_reply->atom;
    free(atom_reply);
    if (!supports_ewmh_active_window()) {
        std::cerr << "WARNING: The current window manager does not support the"
                  << std::endl;
        std::cerr << "         '" << active_window_property_name
                  << "' property. Falling back to a" << std::endl;
        std::cerr << "         less reliable keyboard input grabbing method."
                  << std::endl;
    }

    // If the `editor_force_dnd` option is set, we'll strip `XdndAware` from all
    // of `wine_window`'s ancestors (including `parent_window`) to forcefully
    // enable drag-and-drop support in REAPER. See the docstring on
    // `Configuration::editor_force_dnd` and the option description in the
    // readme for more information.
    if (config.editor_force_dnd) {
        atom_cookie = xcb_intern_atom(x11_connection.get(), true,
                                      strlen(x_dnd_aware_property_name),
                                      x_dnd_aware_property_name);
        atom_reply =
            xcb_intern_atom_reply(x11_connection.get(), atom_cookie, &error);
        THROW_X11_ERROR(error);

        for (const xcb_window_t& window :
             find_ancestor_windows(*x11_connection, parent_window)) {
            xcb_delete_property(x11_connection.get(), window, atom_reply->atom);
        }

        free(atom_reply);
    }

    // When using XEmbed we'll need the atoms for the corresponding properties
    atom_cookie =
        xcb_intern_atom(x11_connection.get(), true, strlen(xembed_message_name),
                        xembed_message_name);
    atom_reply =
        xcb_intern_atom_reply(x11_connection.get(), atom_cookie, &error);
    THROW_X11_ERROR(error);

    xcb_xembed_message = atom_reply->atom;
    free(atom_reply);

    // When not using XEmbed, Wine will interpret any local coordinates as
    // global coordinates. To work around this we'll tell the Wine window it's
    // located at its actual coordinates on screen rather than somewhere within.
    // For robustness's sake this should be done both when the actual window the
    // Wine window is embedded in (which may not be the parent window) is moved
    // or resized, and when the user moves his mouse over the window because
    // this is sometimes needed for plugin groups. We also listen for
    // EnterNotify and LeaveNotify events on the Wine window so we can grab and
    // release input focus as necessary.
    // If we do enable XEmbed support, we'll also listen for visibility changes
    // and trigger the embedding when the window becomes visible
    const uint32_t topmost_event_mask =
        XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_VISIBILITY_CHANGE;
    xcb_change_window_attributes(x11_connection.get(), topmost_window,
                                 XCB_CW_EVENT_MASK, &topmost_event_mask);
    xcb_flush(x11_connection.get());
    const uint32_t parent_event_mask =
        XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_ENTER_WINDOW |
        XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_VISIBILITY_CHANGE;
    xcb_change_window_attributes(x11_connection.get(), parent_window,
                                 XCB_CW_EVENT_MASK, &parent_event_mask);
    xcb_flush(x11_connection.get());

    if (use_xembed) {
        // This call alone doesn't do anything. We need to call this function a
        // second time on visibility change because Wine's XEmbed implementation
        // does not work properly (which is why we remvoed XEmbed support in the
        // first place).
        do_xembed();
    } else {
        // Embed the Win32 window into the window provided by the host. Instead
        // of using the XEmbed protocol, we'll register a few events and manage
        // the child window ourselves. This is a hack to work around the issue's
        // described in `Editor`'s docstring'.
        xcb_reparent_window(x11_connection.get(), wine_window, parent_window, 0,
                            0);
        xcb_flush(x11_connection.get());

        // If we're using the double embedding option, then the child window
        // should only be created after the parent window is visible
        ShowWindow(win32_window.handle, SW_SHOWNORMAL);
        if (config.editor_double_embed) {
            // As explained above, we can't do this directly in the initializer
            // list
            win32_child_window.emplace(
                main_context, x11_connection,
                CreateWindowEx(WS_EX_TOOLWINDOW,
                               reinterpret_cast<LPCSTR>(get_window_class()),
                               "yabridge plugin child", WS_CHILD, CW_USEDEFAULT,
                               CW_USEDEFAULT, client_area.width,
                               client_area.height, win32_window.handle, nullptr,
                               GetModuleHandle(nullptr), this));

            ShowWindow(win32_child_window->handle, SW_SHOWNORMAL);
        }

        // HACK: I can't seem to figure why the initial reparent would fail on
        //       this particular i3 config in a way that I'm unable to
        //       reproduce, but if it doesn't work the first time, just keep
        //       trying!
        //
        //       https://github.com/robot-vdh/yabridge/issues/40
        xcb_reparent_window(x11_connection.get(), wine_window, parent_window, 0,
                            0);
        xcb_flush(x11_connection.get());
    }
}

HWND Editor::get_win32_handle() const noexcept {
    // FIXME: The double embed and XEmbed options don't work together right now
    if (win32_child_window && !use_xembed) {
        return win32_child_window->handle;
    } else {
        return win32_window.handle;
    }
}

void Editor::handle_x11_events() const noexcept {
    // NOTE: Ardour will unmap the window instead of closing the editor. When
    //       the window is unmapped `wine_window` doesn't exist and any X11
    //       function calls involving it will fail. All functions called from
    //       here should be able to handle that cleanly.
    try {
        xcb_generic_event_t* generic_event;
        while ((generic_event = xcb_poll_for_event(x11_connection.get())) !=
               nullptr) {
            const uint8_t event_type =
                generic_event->response_type & event_type_mask;
            switch (event_type) {
                // We're listening for `ConfigureNotify` events on the topmost
                // window before the root window, i.e. the window that's
                // actually going to get dragged around the by the user. In most
                // cases this is the same as `parent_window`. When either this
                // window gets moved, or when the user moves his mouse over our
                // window, the local coordinates should be updated. The
                // additional `EnterWindow` check is sometimes necessary for
                // using multiple editor windows within a single plugin group.
                case XCB_CONFIGURE_NOTIFY:
                    if (!use_xembed) {
                        fix_local_coordinates();
                    }
                    break;
                // Start the XEmbed procedure when the window becomes visible,
                // since most hosts will only show the window after the plugin
                // has embedded itself into it.
                case XCB_VISIBILITY_NOTIFY:
                    if (use_xembed) {
                        do_xembed();
                    }
                    break;
                // We want to grab keyboard input focus when the user hovers
                // over our embedded Wine window AND that window is a child of
                // the currently active window. This ensures that the behavior
                // is similar to what you'd expect of a native application,
                // without grabbing input focus when accidentally hovering over
                // a yabridge window in the background. The `FocusIn` is needed
                // for when returning to the main plugin window after closing a
                // dialog, since that often won't trigger an `EnterNotify'.
                case XCB_ENTER_NOTIFY:
                case XCB_FOCUS_IN:
                    if (!use_xembed) {
                        fix_local_coordinates();
                    }

                    // In case the WM somehow does not support
                    // `_NET_ACTIVE_WINDOW`, a more naive focus grabbing method
                    // implemented in the `WM_PARENTNOTIFY` handler will be
                    // used.
                    if (supports_ewmh_active_window() &&
                        is_wine_window_active()) {
                        set_input_focus(true);
                    }
                    break;
                // When the user moves their mouse away from the Wine window
                // _while the window provided by the host it is contained in is
                // still active_, we will give back keyboard focus to that
                // window. This for instance allows you to still use the search
                // bar in REAPER's FX window. This distinction is important,
                // because we do not want to mess with keyboard focus when
                // hovering over the window while for instance a dialog is open.
                case XCB_LEAVE_NOTIFY: {
                    const auto event =
                        reinterpret_cast<xcb_leave_notify_event_t*>(
                            generic_event);

                    // This extra check for the `NonlinearVirtual` detail is
                    // important (see
                    // https://www.x.org/releases/X11R7.5/doc/x11proto/proto.html
                    // for more information on what this actually means). I've
                    // only seen this issue with the Tokyo Dawn Records plugins,
                    // but a plugin may create a popup window that acts as a
                    // dropdown without actually activating that window (unlike
                    // with an actual Win32 dropdown menu). Without this check
                    // these fake dropdowns would immediately close when
                    // hovering over them.
                    if (event->detail != XCB_NOTIFY_DETAIL_NONLINEAR_VIRTUAL &&
                        supports_ewmh_active_window() &&
                        is_wine_window_active()) {
                        set_input_focus(false);
                    }
                } break;
            }

            free(generic_event);
        }
    } catch (const std::runtime_error& error) {
        std::cerr << error.what() << std::endl;
    }
}

void Editor::fix_local_coordinates() const {
    if (use_xembed) {
        return;
    }

    // We're purposely not using XEmbed here. This has the consequence that wine
    // still thinks that any X and Y coordinates are relative to the x11 window
    // root instead of the parent window provided by the DAW, causing all sorts
    // of GUI interactions to break. To alleviate this we'll just lie to Wine
    // and tell it that it's located at the parent window's location on the root
    // window. We also will keep the child window at its largest possible size
    // to allow for smooth resizing. This works because the embedding hierarchy
    // is DAW window -> Win32 window (created in this class) -> VST plugin
    // window created by the plugin itself. In this case it doesn't matter that
    // the Win32 window is larger than the part of the client area the plugin
    // draws to since any excess will be clipped off by the parent window.
    const xcb_window_t root = get_root_window(*x11_connection, parent_window);

    // We can't directly use the `event.x` and `event.y` coordinates because the
    // parent window may also be embedded inside another window.
    // NOTE: Tracktion Waveform uses client side decorations, and for VST2
    //       plugins they forgot to add a separate parent window that's already
    //       offset correctly. Instead, they'll have the plugin embed itself
    //       inside directly inside of the dialog, and Waveform then moves the
    //       window 27 pixels down. That's why we cannot use `parent_window`
    //       here.
    xcb_generic_error_t* error;
    const xcb_translate_coordinates_cookie_t translate_cookie =
        xcb_translate_coordinates(x11_connection.get(), wine_window, root, 0,
                                  0);
    xcb_translate_coordinates_reply_t* translated_coordinates =
        xcb_translate_coordinates_reply(x11_connection.get(), translate_cookie,
                                        &error);
    THROW_X11_ERROR(error);

    xcb_configure_notify_event_t translated_event{};
    translated_event.response_type = XCB_CONFIGURE_NOTIFY;
    translated_event.event = wine_window;
    translated_event.window = wine_window;
    // This should be set to the same sizes the window was created on. Since
    // we're not using `SetWindowPos` to resize the Window, Wine can get a bit
    // confused when we suddenly report a different client area size. Without
    // this certain plugins (such as those by Valhalla DSP) would break.
    translated_event.width = client_area.width;
    translated_event.height = client_area.height;
    translated_event.x = translated_coordinates->dst_x;
    translated_event.y = translated_coordinates->dst_y;
    free(translated_coordinates);

    xcb_send_event(
        x11_connection.get(), false, wine_window,
        XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
        reinterpret_cast<char*>(&translated_event));
    xcb_flush(x11_connection.get());
}

void Editor::set_input_focus(bool grab) const {
    const xcb_window_t focus_target = grab ? parent_window : topmost_window;

    xcb_generic_error_t* error;
    const xcb_get_input_focus_cookie_t focus_cookie =
        xcb_get_input_focus(x11_connection.get());
    xcb_get_input_focus_reply_t* focus_reply =
        xcb_get_input_focus_reply(x11_connection.get(), focus_cookie, &error);
    THROW_X11_ERROR(error);

    const xcb_window_t current_focus = focus_reply->focus;
    free(focus_reply);

    // Calling `set_input_focus(true)` can trigger another `FocusIn` event,
    // which will then once again call `set_input_focus(true)`. To work around
    // this we prevent unnecessary repeat keyboard focus grabs.
    // One thing that slightly complicates this is the use of unmapped input
    // proxy windows. When `topmost_window` gets foccused, some hosts will
    // reassign input focus to such a proxy window. To avoid fighting over
    // focus, when grabbing focus we don't just check whether `current_focus`
    // and `focus_target` are the same window but we'll also allow
    // `current_focus` to be a child of `focus_target`.
    if (current_focus == focus_target ||
        (grab && is_child_window_or_same(*x11_connection, current_focus,
                                         focus_target))) {
        return;
    }

    // Explicitly request input focus when the user interacts with the window.
    // Without this `topmost_window` will capture all keyboard events in most
    // hosts. Ideally we would just do this whenever the child window calls
    // `SetFocus()` (or no handling should be necessary), but as far as I'm
    // aware there is no way to do this. Right now we will grab input focus when
    // the user hovers over the Wine window while the window it is contained in
    // (the one provided by the host) is active. Keyboard focus will be given
    // back to that window when the user moves their mouse outside of the Wine
    // window while the host's window is still active (that's an important
    // detail, since plugins may have dialogs).
    // XXX: In theory we wouldn't have to do this for VST3 because
    //      `IPlugView::onKey{Down,Up}` should handle all keyboard events. But
    //      in practice a lot of hosts don't use that, so we still need to grab
    //      focus ourselves.
    xcb_set_input_focus(x11_connection.get(), XCB_INPUT_FOCUS_PARENT,
                        focus_target, XCB_CURRENT_TIME);
    xcb_flush(x11_connection.get());
}

void Editor::maybe_run_timer_proc() {
    if (idle_timer_proc) {
        (*idle_timer_proc)();
    }
}

bool Editor::is_wine_window_active() const {
    if (!supports_ewmh_active_window()) {
        return false;
    }

    // We will only grab focus when the Wine window is active. To do this we'll
    // read the `_NET_ACTIVE_WINDOW` property from the root window (which can
    // change when the window gets moved to another screen, so we won't cache
    // this).
    const xcb_window_t root_window =
        get_root_window(*x11_connection, wine_window);

    xcb_generic_error_t* error;
    const xcb_get_property_cookie_t property_cookie =
        xcb_get_property(x11_connection.get(), false, root_window,
                         active_window_property, XCB_ATOM_WINDOW, 0, 1);
    xcb_get_property_reply_t* property_reply =
        xcb_get_property_reply(x11_connection.get(), property_cookie, &error);
    THROW_X11_ERROR(error);

    const xcb_window_t active_window =
        *static_cast<xcb_window_t*>(xcb_get_property_value(property_reply));
    free(property_reply);

    return is_child_window_or_same(*x11_connection, wine_window, active_window);
}

bool Editor::supports_ewmh_active_window() const {
    if (supports_ewmh_active_window_cache) {
        return *supports_ewmh_active_window_cache;
    }

    // It could be that the `_NET_ACTIVE_WINDOW` atom exists (because it was
    // created by another application) but that the root window does not have
    // the property
    if (active_window_property == XCB_ATOM_NONE) {
        supports_ewmh_active_window_cache = false;
        return false;
    }

    const xcb_window_t root_window =
        get_root_window(*x11_connection, wine_window);

    // If the `_NET_ACTIVE_WINDOW` property does not exist on the root window,
    // the returned property type will be `XCB_ATOM_NONE` as specified in the
    // X11 manual
    xcb_generic_error_t* error;
    const xcb_get_property_cookie_t property_cookie =
        xcb_get_property(x11_connection.get(), false, root_window,
                         active_window_property, XCB_ATOM_WINDOW, 0, 1);
    xcb_get_property_reply_t* property_reply =
        xcb_get_property_reply(x11_connection.get(), property_cookie, &error);
    THROW_X11_ERROR(error);

    bool active_window_property_exists =
        property_reply->format != XCB_ATOM_NONE;
    free(property_reply);

    supports_ewmh_active_window_cache = active_window_property_exists;
    return active_window_property_exists;
}

void Editor::send_xembed_message(const xcb_window_t& window,
                                 const uint32_t message,
                                 const uint32_t detail,
                                 const uint32_t data1,
                                 const uint32_t data2) const noexcept {
    xcb_client_message_event_t event;
    event.response_type = XCB_CLIENT_MESSAGE;
    event.type = xcb_xembed_message;
    event.window = window;
    event.format = 32;
    event.data.data32[0] = XCB_CURRENT_TIME;
    event.data.data32[1] = message;
    event.data.data32[2] = detail;
    event.data.data32[3] = data1;
    event.data.data32[4] = data2;

    xcb_send_event(x11_connection.get(), false, window, XCB_EVENT_MASK_NO_EVENT,
                   reinterpret_cast<char*>(&event));
}

void Editor::do_xembed() const {
    if (!use_xembed) {
        return;
    }

    // If we're embedding using XEmbed, then we'll have to go through the whole
    // XEmbed dance here. See the spec for more information on how this works:
    // https://specifications.freedesktop.org/xembed-spec/xembed-spec-latest.html#lifecycle
    xcb_reparent_window(x11_connection.get(), wine_window, parent_window, 0, 0);
    xcb_flush(x11_connection.get());

    // Let the Wine window know it's being embedded into the parent window
    send_xembed_message(wine_window, xembed_embedded_notify_msg, 0,
                        parent_window, xembed_protocol_version);
    send_xembed_message(wine_window, xembed_focus_in_msg, xembed_focus_first, 0,
                        0);
    send_xembed_message(wine_window, xembed_window_activate_msg, 0, 0, 0);
    xcb_flush(x11_connection.get());

    xcb_map_window(x11_connection.get(), wine_window);
    xcb_flush(x11_connection.get());

    ShowWindow(win32_window.handle, SW_SHOWNORMAL);
}

LRESULT CALLBACK window_proc(HWND handle,
                             UINT message,
                             WPARAM wParam,
                             LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            const auto window_parameters =
                reinterpret_cast<CREATESTRUCT*>(lParam);
            const auto editor =
                static_cast<Editor*>(window_parameters->lpCreateParams);
            if (!editor) {
                break;
            }

            // Sent when the window is first being created. `lParam` here
            // contains the last argument of `CreateWindowEx`, which was a
            // pointer to the `Editor` object. We need to attach this to the
            // window handle so we can access our VST plugin instance later.
            SetWindowLongPtr(
                handle, GWLP_USERDATA,
                static_cast<LONG_PTR>(reinterpret_cast<size_t>(editor)));
        } break;
        // Setting `SWP_NOCOPYBITS` somewhat reduces flickering on
        // `fix_local_coordinates()` calls with plugins that don't do double
        // buffering since it speeds up the redrawing process.
        case WM_WINDOWPOSCHANGING: {
            auto editor = reinterpret_cast<Editor*>(
                GetWindowLongPtr(handle, GWLP_USERDATA));
            if (!editor || editor->use_xembed) {
                break;
            }

            WINDOWPOS* info = reinterpret_cast<WINDOWPOS*>(lParam);
            info->flags |= SWP_NOCOPYBITS | SWP_DEFERERASE;
        } break;
        case WM_TIMER: {
            auto editor = reinterpret_cast<Editor*>(
                GetWindowLongPtr(handle, GWLP_USERDATA));
            if (!editor || wParam != idle_timer_id) {
                break;
            }

            // We'll send idle messages on a timer for VST2 plugins. This way
            // the plugin will get keep periodically updating its editor either
            // when the host sends `effEditIdle` themself, or periodically when
            // the GUI is being blocked by a dropdown or a message box.
            editor->maybe_run_timer_proc();
            return 0;
        } break;
        // In case the WM does not support the EWMH active window property,
        // we'll fall back to grabbing focus when the user clicks on the window
        // by listening to the generated `WM_PARENTNOTIFY` messages. Otherwise
        // we have some more sophisticated behaviour using `EnterNotify` and
        // `LeaveNotify` X11 events. This will only be necessary for very
        // barebones window managers.
        case WM_PARENTNOTIFY: {
            auto editor = reinterpret_cast<Editor*>(
                GetWindowLongPtr(handle, GWLP_USERDATA));
            if (!editor || editor->supports_ewmh_active_window()) {
                break;
            }

            editor->set_input_focus(true);
        } break;
    }

    return DefWindowProc(handle, message, wParam, lParam);
}

std::vector<xcb_window_t> find_ancestor_windows(
    xcb_connection_t& x11_connection,
    xcb_window_t starting_at) {
    xcb_window_t current_window = starting_at;
    std::vector<xcb_window_t> ancestor_windows{current_window};

    xcb_generic_error_t* error;
    xcb_query_tree_cookie_t query_cookie =
        xcb_query_tree(&x11_connection, starting_at);
    xcb_query_tree_reply_t* query_reply =
        xcb_query_tree_reply(&x11_connection, query_cookie, &error);
    THROW_X11_ERROR(error);

    xcb_window_t root = query_reply->root;
    while (query_reply->parent != root) {
        current_window = query_reply->parent;
        ancestor_windows.push_back(current_window);

        free(query_reply);
        query_cookie = xcb_query_tree(&x11_connection, current_window);
        query_reply =
            xcb_query_tree_reply(&x11_connection, query_cookie, &error);
        THROW_X11_ERROR(error);
    }

    free(query_reply);
    return ancestor_windows;
}

bool is_child_window_or_same(xcb_connection_t& x11_connection,
                             xcb_window_t child,
                             xcb_window_t parent) {
    xcb_window_t current_window = child;

    xcb_generic_error_t* error;
    xcb_query_tree_cookie_t query_cookie =
        xcb_query_tree(&x11_connection, child);
    xcb_query_tree_reply_t* query_reply =
        xcb_query_tree_reply(&x11_connection, query_cookie, &error);
    THROW_X11_ERROR(error);

    while (query_reply->parent != XCB_NONE) {
        if (current_window == parent) {
            free(query_reply);
            return true;
        }

        current_window = query_reply->parent;

        free(query_reply);
        query_cookie = xcb_query_tree(&x11_connection, current_window);
        query_reply =
            xcb_query_tree_reply(&x11_connection, query_cookie, &error);
        THROW_X11_ERROR(error);
    }

    free(query_reply);
    return false;
}

Size get_maximum_screen_dimensions(xcb_connection_t& x11_connection) noexcept {
    xcb_screen_iterator_t iter =
        xcb_setup_roots_iterator(xcb_get_setup(&x11_connection));

    // Find the maximum dimensions the window would have to be to be able to be
    // fullscreened on any screen, disregarding the possibility that someone
    // would try to stretch the window accross all displays (because who would
    // do such a thing?)
    Size maximum_screen_size{};
    while (iter.rem > 0) {
        maximum_screen_size.width =
            std::max(maximum_screen_size.width, iter.data->width_in_pixels);
        maximum_screen_size.height =
            std::max(maximum_screen_size.height, iter.data->height_in_pixels);

        xcb_screen_next(&iter);
    }

    return maximum_screen_size;
}

xcb_window_t get_root_window(xcb_connection_t& x11_connection,
                             xcb_window_t window) {
    xcb_generic_error_t* error;
    const xcb_query_tree_cookie_t query_cookie =
        xcb_query_tree(&x11_connection, window);
    xcb_query_tree_reply_t* query_reply =
        xcb_query_tree_reply(&x11_connection, query_cookie, &error);
    THROW_X11_ERROR(error);

    const xcb_window_t root = query_reply->root;
    free(query_reply);

    return root;
}

xcb_window_t get_x11_handle(HWND win32_handle) noexcept {
    return reinterpret_cast<size_t>(
        GetProp(win32_handle, "__wine_x11_whole_window"));
}

ATOM get_window_class() noexcept {
    // Lazily iniitialize our window class
    static ATOM window_class_handle = 0;
    if (!window_class_handle) {
        WNDCLASSEX window_class{};

        // XXX: We could also add a background here. This would get rid of any
        //      artifacts on hosts that don't resize the window properly (e.g.
        //      REAPER with VST2 plugins), but it can also cause that background
        //      to briefly become visible on a call to `fix_local_coordinates()`
        //      which can look very jarring.
        window_class.cbSize = sizeof(WNDCLASSEX);
        window_class.style = CS_DBLCLKS;
        window_class.lpfnWndProc = window_proc;
        window_class.hInstance = GetModuleHandle(nullptr);
        window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
        window_class.lpszClassName = "yabridge plugin";

        window_class_handle = RegisterClassEx(&window_class);
    }

    return window_class_handle;
}

#undef THROW_X11_ERROR
