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

// The ARA SDK headers themselves include VST3 SDK headers, so they must come
// after funknown.h. They also use some pragmas that may not be defined yet.
// ARAVST3.h only defines the ARA::IMainFactory, IPlugInEntryPoint, and
// IPlugInEntryPoint2 interfaces; we only need FUnknown and the IIDs here.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wextra-semi"
#pragma GCC diagnostic ignored "-Wundef"
#include <ARA_API/ARAVST3.h>
#pragma GCC diagnostic pop

#include "../../common.h"
#include "../base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `ARA::IPlugInEntryPoint` for detection purposes. This is
 * instantiated as part of `Vst3PluginProxy`.
 *
 * ARA (Audio Random Access) is a Celemony extension to VST3 that allows
 * hosts like Logic Pro, Studio One, and Cubase to perform advanced audio
 * analysis (e.g. tempo detection, pitch correction) on audio material
 * independently of real-time playback. Plugins implementing this interface
 * (e.g. Melodyne, Elastique) attach an `IPlugInEntryPoint` to their
 * `IAudioProcessor` component.
 *
 * This wrapper only implements detection (`ConstructArgs`) — no method
 * serialization structs are defined yet, so the interface is never actually
 * proxied across the bridge.
 */
class YaARAPlugInEntryPoint : public ARA::IPlugInEntryPoint {
   public:
    /**
     * These are the arguments for creating a `YaARAPlugInEntryPoint`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements
         * `ARA::IPlugInEntryPoint` and read arguments from it.
         * Logs a message if ARA support is detected.
         */
        ConstructArgs(Steinberg::IPtr<Steinberg::FUnknown> object) noexcept;

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
    YaARAPlugInEntryPoint(ConstructArgs&& args) noexcept;

    virtual ~YaARAPlugInEntryPoint() noexcept = default;

    inline bool supported() const noexcept { return arguments_.supported; }

    // ARA::IPlugInEntryPoint pure virtual methods — not yet implemented.
    // These must be declared to satisfy the abstract base class, but they will
    // never be called because `queryInterface()` in `plugin-proxy.cpp` will
    // not return this interface until it is fully proxied.
    virtual const ARAFactory* PLUGIN_API getFactory() override = 0;
    ARA_DEPRECATED(2_0_Draft)
    virtual const ARAPlugInExtensionInstance* PLUGIN_API
    bindToDocumentController(
        ARADocumentControllerRef documentControllerRef) override = 0;

   protected:
    ConstructArgs arguments_;
};

/**
 * Wraps around `ARA::IPlugInEntryPoint2` for detection purposes. This is
 * instantiated as part of `Vst3PluginProxy`.
 *
 * `IPlugInEntryPoint2` is the ARA 2.0 extension of `IPlugInEntryPoint`. It
 * adds `bindToDocumentControllerWithRoles()`, which allows the host to assign
 * specific roles (playback renderer, edit renderer, editor view) to each
 * plugin instance.
 *
 * Like `YaARAPlugInEntryPoint`, this wrapper only implements detection.
 */
class YaARAPlugInEntryPoint2 : public ARA::IPlugInEntryPoint2 {
   public:
    /**
     * These are the arguments for creating a `YaARAPlugInEntryPoint2`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements
         * `ARA::IPlugInEntryPoint2` and read arguments from it.
         * Logs a message if ARA 2.0 support is detected.
         */
        ConstructArgs(Steinberg::IPtr<Steinberg::FUnknown> object) noexcept;

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
    YaARAPlugInEntryPoint2(ConstructArgs&& args) noexcept;

    virtual ~YaARAPlugInEntryPoint2() noexcept = default;

    inline bool supported() const noexcept { return arguments_.supported; }

    // ARA::IPlugInEntryPoint2 pure virtual method — not yet implemented.
    virtual const ARAPlugInExtensionInstance* PLUGIN_API
    bindToDocumentControllerWithRoles(
        ARADocumentControllerRef documentControllerRef,
        ARAPlugInInstanceRoleFlags knownRoles,
        ARAPlugInInstanceRoleFlags assignedRoles) override = 0;

   protected:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop
