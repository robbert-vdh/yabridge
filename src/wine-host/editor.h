#include <xcb/xcb.h>

#define NOMINMAX
#define NOSERVICE
#define NOMCX
#define NOIMM
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <memory>
#include <optional>
#include <string>

/**
 * A wrapper around the win32 windowing API to create and destroy editor
 * windows. A VST plugin can embed itself in that window, and we can then later
 * embed the window in a VST host provided X11 window.
 */
class Win32Editor {
   public:
    /**
     * @param window_class_name The name for the window class for editor
     *   windows.
     */
    Win32Editor(std::string window_class_name);

    /**
     * Open a window and return a handle to the new Win32 window that can be
     * used by the hosted VST plugin.
     */
    HWND open();
    void close();

    /**
     * Return the X11 window handle for the window if it's currently open.
     */
    std::optional<xcb_window_t> get_x11_handle();

   private:
    ATOM window_class;

    /**
     * A pointer to the currently active window. Will be a null pointer if no
     * window is active.
     */
    std::optional<
        std::unique_ptr<std::remove_pointer_t<HWND>, decltype(&DestroyWindow)>>
        window_handle;

    std::unique_ptr<xcb_connection_t, decltype(&xcb_disconnect)> x11_connection;
};
