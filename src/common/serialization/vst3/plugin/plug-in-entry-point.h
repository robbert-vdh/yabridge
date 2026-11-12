// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2026 Robbert van der Helm
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

#include <pluginterfaces/base/funknown.h>

#ifdef WITH_ARA
#include <variant>

#include <ARAVST3.h>

#include "../../../bitsery/ext/in-place-variant.h"

#include "ara-factory.h"
#endif

#include "../../common.h"
#include "../base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Detects whether an `IComponent` object supports the ARA plug-in entry point
 * interfaces (`ARA::IPlugInEntryPoint` and `ARA::IPlugInEntryPoint2`).
 *
 * When ARA support is not compiled in (`WITH_ARA` not defined) this class
 * still exists so that `Vst3PluginProxy::ConstructArgs` can carry the
 * detection result across the wire and the host can log that the plugin
 * advertises ARA capability.
 */
class YaPlugInEntryPoint {
   public:
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Query `IPlugInEntryPoint` and `IPlugInEntryPoint2` support from an
         * existing object.  When `WITH_ARA` is not defined both fields are
         * always false.
         */
        ConstructArgs(Steinberg::IPtr<Steinberg::FUnknown> object) noexcept;

        /**
         * Whether the object supports `ARA::IPlugInEntryPoint` (ARA 1,
         * deprecated but still encountered in the wild).
         */
        bool supports_plug_in_entry_point = false;

        /**
         * Whether the object supports `ARA::IPlugInEntryPoint2` (ARA 2+,
         * the interface that should actually be used).
         */
        bool supports_plug_in_entry_point_2 = false;

        template <typename S>
        void serialize(S& s) {
            s.value1b(supports_plug_in_entry_point);
            s.value1b(supports_plug_in_entry_point_2);
        }
    };

    explicit YaPlugInEntryPoint(ConstructArgs&& args) noexcept;
    virtual ~YaPlugInEntryPoint() noexcept = default;

    inline bool ara_supported() const noexcept {
        return arguments_.supports_plug_in_entry_point_2 ||
               arguments_.supports_plug_in_entry_point;
    }

    inline bool supports_plug_in_entry_point() const noexcept {
        return arguments_.supports_plug_in_entry_point;
    }

    inline bool supports_plug_in_entry_point_2() const noexcept {
        return arguments_.supports_plug_in_entry_point_2;
    }

#ifdef WITH_ARA
    // The legacy ARA1 role set used by bindToDocumentController().
    // Shared between the Linux proxy and Wine-side handler.
    static constexpr ARA::ARAInt32 kARALegacyRoles =
        ARA::kARAPlaybackRendererRole | ARA::kARAEditorRendererRole |
        ARA::kARAEditorViewRole;

    struct GetFactory {
        using Response = std::variant<YaAraFactory, UniversalTResult>;

        native_size_t instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
        }
    };

    struct BindToDocumentControllerWithRoles {
        using Response =
            std::variant<YaAraPlugInExtensionInstance, UniversalTResult>;

        native_size_t instance_id;
        native_size_t ara_dc_id;
        ARA::ARAInt32 known_roles;
        ARA::ARAInt32 assigned_roles;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value8b(ara_dc_id);
            s.value4b(known_roles);
            s.value4b(assigned_roles);
        }
    };
#endif  // WITH_ARA

   protected:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop

#ifdef WITH_ARA

template <typename S>
void serialize(S& s, std::variant<YaAraFactory, UniversalTResult>& result) {
    s.ext(result, bitsery::ext::InPlaceVariant{});
}

template <typename S>
void serialize(
    S& s,
    std::variant<YaAraPlugInExtensionInstance, UniversalTResult>& result) {
    s.ext(result, bitsery::ext::InPlaceVariant{});
}

#endif  // WITH_ARA
