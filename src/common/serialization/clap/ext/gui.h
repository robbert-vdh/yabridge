// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2022 Robbert van der Helm
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more deguis.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <optional>

#include <clap/ext/gui.h>

#include "../../../bitsery/ext/in-place-optional.h"
#include "../../common.h"

// Serialization messages for `clap/ext/gui.h`

// Like the VST3 GUI handling, we'll translate the "X11" API type to "HWND" on
// the Wine side.

// TODO: We only support the embedded parts of the API on both sides right now.
//       Floating modes where the plugin window is still embedded and the
//       toplevel Wine window is floating would be possible. Having the plugin's
//       own window be floating all by itself is also possible, but then
//       `set_transient()` wouldn't be possible which would make it awkward to
//       use.

namespace clap {
namespace ext {
namespace gui {

/**
 * The API types we can embed windows for. Technically we could also allow the
 * host to send a HWND directly if it also did some Winelib trickery, but
 * realistically that won't ever happen.
 */
enum class ApiType : uint32_t { X11 };

namespace plugin {

/**
 * Message struct for `clap_plugin_gui::is_api_supported()`.
 */
struct IsApiSupported {
    using Response = PrimitiveResponse<bool>;

    native_size_t instance_id;
    /**
     * This will always be X11, we'll currently ignore anything else. X11 gets
     * translated to HWND before passing it to the plugin.
     */
    ApiType api;
    /**
     * This will always be false, we'll currently ignore anything else.
     */
    bool is_floating;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value4b(api);
        s.value1b(is_floating);
    }
};

// NOTE: We currently don't bridge `clap_plugin_gui::get_preferred_api()` since
//       it doesn't make much sense. We'll always return X11 floating from
//       there.

/**
 * Message struct for `clap_plugin_gui::create()`.
 */
struct Create {
    using Response = PrimitiveResponse<bool>;

    native_size_t instance_id;
    /**
     * This will always be X11, we'll currently ignore anything else. X11 gets
     * translated to HWND before passing it to the plugin.
     */
    ApiType api;
    /**
     * This will always be false, we'll currently ignore anything else.
     */
    bool is_floating;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value4b(api);
        s.value1b(is_floating);
    }
};

/**
 * Message struct for `clap_plugin_gui::destroy()`.
 */
struct Destroy {
    using Response = Ack;

    native_size_t instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
    }
};

/**
 * Message struct for `clap_plugin_gui::set_scale()`.
 */
struct SetScale {
    using Response = PrimitiveResponse<bool>;

    native_size_t instance_id;
    double scale;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value8b(scale);
    }
};

/**
 * The response to the `clap::ext::params::plugin::GetSize` message defined
 * below.
 */
struct GetSizeResponse {
    bool result;

    uint32_t width;
    uint32_t height;

    template <typename S>
    void serialize(S& s) {
        s.value1b(result);
        s.value4b(width);
        s.value4b(height);
    }
};

/**
 * Message struct for `clap_plugin_gui::get_size()`.
 */
struct GetSize {
    using Response = GetSizeResponse;

    native_size_t instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
    }
};

/**
 * Message struct for `clap_plugin_gui::can_resize()`.
 */
struct CanResize {
    using Response = PrimitiveResponse<bool>;

    native_size_t instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
    }
};

/**
 * The response to the `clap::ext::params::plugin::GetResizeHints` message
 * defined below.
 */
struct GetResizeHintsResponse {
    // This doesn't require a special wrapper since the struct only contains
    // primitive values. Its serialization function is defined at the bottom of
    // this header.
    std::optional<clap_gui_resize_hints_t> result;

    template <typename S>
    void serialize(S& s) {
        s.ext(result, bitsery::ext::InPlaceOptional());
    }
};

/**
 * Message struct for `clap_plugin_gui::get_resize_hints()`.
 */
struct GetResizeHints {
    using Response = GetResizeHintsResponse;

    native_size_t instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
    }
};

/**
 * The response to the `clap::ext::params::plugin::AdjustSize` message defined
 * below.
 */
struct AdjustSizeResponse {
    bool result;

    uint32_t updated_width;
    uint32_t updated_height;

    template <typename S>
    void serialize(S& s) {
        s.value1b(result);
        s.value4b(updated_width);
        s.value4b(updated_height);
    }
};

/**
 * Message struct for `clap_plugin_gui::adjust_size()`.
 */
struct AdjustSize {
    using Response = AdjustSizeResponse;

    native_size_t instance_id;

    uint32_t width;
    uint32_t height;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value4b(width);
        s.value4b(height);
    }
};

/**
 * Message struct for `clap_plugin_gui::set_size()`.
 */
struct SetSize {
    using Response = PrimitiveResponse<bool>;

    native_size_t instance_id;

    uint32_t width;
    uint32_t height;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value4b(width);
        s.value4b(height);
    }
};

/**
 * Message struct for `clap_plugin_gui::set_parent()`.
 */
struct SetParent {
    using Response = PrimitiveResponse<bool>;

    native_size_t instance_id;

    // We only support X11 right now, so we can simplify this a little
    // NOTE: This should be a `clap_xwnd`, but that's defined as an `unsigned
    //       long` which is 32-bit on Windows and 64-bit on Linux
    uint64_t x11_window;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value8b(x11_window);
    }
};

// NOTE: There are no structs for `clap_plugin_gui::set_transient()` or
//       `clap_plugin_gui::suggest_title()` since Wine-only floating windows
//       wouldn't be able to set the transient window (which would be an X11
//       window).

/**
 * Message struct for `clap_plugin_gui::show()`.
 */
struct Show {
    using Response = PrimitiveResponse<bool>;

    native_size_t instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
    }
};

/**
 * Message struct for `clap_plugin_gui::hide()`.
 */
struct Hide {
    using Response = PrimitiveResponse<bool>;

    native_size_t instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
    }
};

}  // namespace plugin

namespace host {

/**
 * Message struct for `clap_host_gui::resize_hints_changed()`.
 */
struct ResizeHintsChanged {
    using Response = Ack;

    native_size_t owner_instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(owner_instance_id);
    }
};

/**
 * Message struct for `clap_host_gui::request_resize()`.
 */
struct RequestResize {
    using Response = PrimitiveResponse<bool>;

    native_size_t owner_instance_id;

    uint32_t width;
    uint32_t height;

    template <typename S>
    void serialize(S& s) {
        s.value8b(owner_instance_id);
        s.value4b(width);
        s.value4b(height);
    }
};

/**
 * Message struct for `clap_host_gui::request_show()`.
 */
struct RequestShow {
    using Response = PrimitiveResponse<bool>;

    native_size_t owner_instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(owner_instance_id);
    }
};

/**
 * Message struct for `clap_host_gui::request_hide()`.
 */
struct RequestHide {
    using Response = PrimitiveResponse<bool>;

    native_size_t owner_instance_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(owner_instance_id);
    }
};

/**
 * Message struct for `clap_host_gui::closed()`.
 */
struct Closed {
    using Response = Ack;

    native_size_t owner_instance_id;

    bool was_destroyed;

    template <typename S>
    void serialize(S& s) {
        s.value8b(owner_instance_id);
        s.value1b(was_destroyed);
    }
};

}  // namespace host

}  // namespace gui
}  // namespace ext
}  // namespace clap

template <typename S>
void serialize(S& s, clap_gui_resize_hints_t& hints) {
    s.value1b(hints.can_resize_horizontally);
    s.value1b(hints.can_resize_vertically);
    s.value1b(hints.preserve_aspect_ratio);
    s.value4b(hints.aspect_ratio_width);
    s.value4b(hints.aspect_ratio_height);
}
