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

#pragma once

#include <pluginterfaces/gui/iplugview.h>

#include "../../common.h"
#include "../base.h"
#include "../plug-frame-proxy.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IPlugView` for serialization purposes. This is instantiated as
 * part of `Vst3PlugViewProxy`.
 */
class YaPlugView : public Steinberg::IPlugView {
   public:
    /**
     * These are the arguments for creating a `YaPlugView`.
     */
    struct ConstructArgs {
        ConstructArgs();

        /**
         * Check whether an existing implementation implements `IPlugView` and
         * read arguments from it.
         */
        ConstructArgs(Steinberg::IPtr<Steinberg::FUnknown> object);

        /**
         * Whether the object supported this interface.
         */
        bool supported;

        template <typename S>
        void serialize(S& s) {
            s.value1b(supported);
        }
    };

    /**
     * Instantiate this instance with arguments read from another interface
     * implementation.
     */
    YaPlugView(const ConstructArgs&& args);

    inline bool supported() const { return arguments.supported; }

    /**
     * Message to pass through a call to
     * `IPlugView::isPlatformTypeSupported(type)` to the Wine plugin host. We
     * will of course change `kPlatformStringLinux` for `kPlatformStringWin`,
     * because why would a Windows VST3 plugin have X11 support? (and how would
     * that even work)
     */
    struct IsPlatformTypeSupported {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        std::string type;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.text1b(type, 128);
        }
    };

    virtual tresult PLUGIN_API
    isPlatformTypeSupported(Steinberg::FIDString type) override = 0;

    /**
     * Message to pass through a call to `IPlugView::attached(parent, type)` to
     * the Wine plugin host. Like mentioned above we will substitute
     * `kPlatformStringWin` for `kPlatformStringLinux`.
     */
    struct Attached {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        /**
         * The parent handle passed by the host. This will be an
         * `xcb_window_id`, and we'll embed the Wine window into it ourselves.
         */
        native_size_t parent;
        std::string type;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value8b(parent);
            s.text1b(type, 128);
        }
    };

    virtual tresult PLUGIN_API attached(void* parent,
                                        Steinberg::FIDString type) override = 0;

    /**
     * Message to pass through a call to `IPlugView::removed()` to the Wine
     * plugin host.
     */
    struct Removed {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
        }
    };

    virtual tresult PLUGIN_API removed() override = 0;

    /**
     * Message to pass through a call to `IPlugView::onWheel(distance)` to the
     * Wine plugin host.
     */
    struct OnWheel {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        float distance;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value4b(distance);
        }
    };

    virtual tresult PLUGIN_API onWheel(float distance) override = 0;

    /**
     * Message to pass through a call to `IPlugView::onKeyDown(key, keyCode,
     * modifiers)` to the Wine plugin host.
     */
    struct OnKeyDown {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        char16 key;
        int16 key_code;
        int16 modifiers;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value2b(key);
            s.value2b(key_code);
            s.value2b(modifiers);
        }
    };

    virtual tresult PLUGIN_API onKeyDown(char16 key,
                                         int16 keyCode,
                                         int16 modifiers) override = 0;

    /**
     * Message to pass through a call to `IPlugView::onKeyUp(key, keyCode,
     * modifiers)` to the Wine plugin host.
     */
    struct OnKeyUp {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        char16 key;
        int16 key_code;
        int16 modifiers;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value2b(key);
            s.value2b(key_code);
            s.value2b(modifiers);
        }
    };

    virtual tresult PLUGIN_API onKeyUp(char16 key,
                                       int16 keyCode,
                                       int16 modifiers) override = 0;

    /**
     * The response code and editor size returned by a call to
     * `IPlugView::getSize(&size)`.
     */
    struct GetSizeResponse {
        UniversalTResult result;
        Steinberg::ViewRect updated_size;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.object(updated_size);
        }
    };

    /**
     * Message to pass through a call to `IPlugView::getSize(&size)`.
     */
    struct GetSize {
        using Response = GetSizeResponse;

        native_size_t owner_instance_id;

        Steinberg::ViewRect size;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.object(size);
        }
    };

    virtual tresult PLUGIN_API getSize(Steinberg::ViewRect* size) override = 0;

    /**
     * Message to pass through a call to `IPlugView::onSize(new_size)` to the
     * Wine plugin host.
     */
    struct OnSize {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        Steinberg::ViewRect new_size;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.object(new_size);
        }
    };

    virtual tresult PLUGIN_API
    onSize(Steinberg::ViewRect* newSize) override = 0;

    /**
     * Message to pass through a call to `IPlugView::onFocus(state)` to the Wine
     * plugin host.
     */
    struct OnFocus {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        TBool state;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value1b(state);
        }
    };

    virtual tresult PLUGIN_API onFocus(TBool state) override = 0;

    /**
     * Message to pass through a call to `IPlugView::setFrame()` to the Wine
     * plugin host. We will read what interfaces the passed `IPlugFrame` object
     * implements so we can then create a proxy object on the Wine side that the
     * plugin can use to make callbacks with. The lifetime of this
     * `Vst3PlugFrameProxy` object should be bound to the `IPlugView` we are
     * creating it for.
     */
    struct SetFrame {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        Vst3PlugFrameProxy::ConstructArgs plug_frame_args;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.object(plug_frame_args);
        }
    };

    virtual tresult PLUGIN_API
    setFrame(Steinberg::IPlugFrame* frame) override = 0;

    /**
     * Message to pass through a call to `IPlugView::canResize()` to the Wine
     * plugin host.
     */
    struct CanResize {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
        }
    };

    virtual tresult PLUGIN_API canResize() override = 0;

    /**
     * Message to pass through a call to `IPlugView::checkSizeConstraint(rect)`
     * to the Wine plugin host.
     */
    struct CheckSizeConstraint {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        Steinberg::ViewRect rect;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.object(rect);
        }
    };

    virtual tresult PLUGIN_API
    checkSizeConstraint(Steinberg::ViewRect* rect) override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop

namespace Steinberg {
template <typename S>
void serialize(S& s, ViewRect& rect) {
    s.value4b(rect.left);
    s.value4b(rect.top);
    s.value4b(rect.right);
    s.value4b(rect.bottom);
}
}  // namespace Steinberg
