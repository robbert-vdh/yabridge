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

#include <string>
#include <vector>

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
 * Serializable snapshot of an `ARA::ARAFactory`. This contains only the plain
 * data members, not any of the function pointers.
 */
struct YaARAFactorySnapshot {
    YaARAFactorySnapshot() noexcept;
    explicit YaARAFactorySnapshot(const ARA::ARAFactory* factory) noexcept;

    ARA::ARASize struct_size = 0;
    ARA::ARAAPIGeneration lowest_supported_api_generation =
        static_cast<ARA::ARAAPIGeneration>(0);
    ARA::ARAAPIGeneration highest_supported_api_generation =
        static_cast<ARA::ARAAPIGeneration>(0);

    std::string factory_id;
    std::string plug_in_name;
    std::string manufacturer_name;
    std::string information_url;
    std::string version;

    std::string document_archive_id;
    std::vector<std::string> compatible_document_archive_ids;

    std::vector<ARA::ARAContentType> analyzeable_content_types;
    ARA::ARAPlaybackTransformationFlags supported_playback_transformation_flags =
        static_cast<ARA::ARAPlaybackTransformationFlags>(0);
    ARA::ARABool supports_storing_audio_file_chunks = ARA::kARAFalse;
    ARA::ARABool supports_sample_based_audio_sources = ARA::kARAFalse;
    ARA::ARABool supports_content_only_audio_sources = ARA::kARAFalse;
    ARA::ARABool requires_preset_audio_sources = ARA::kARAFalse;

    template <typename S>
    void serialize(S& s) {
        s.value8b(struct_size);
        s.value4b(lowest_supported_api_generation);
        s.value4b(highest_supported_api_generation);
        s.text1b(factory_id, 1024);
        s.text1b(plug_in_name, 1024);
        s.text1b(manufacturer_name, 1024);
        s.text1b(information_url, 2048);
        s.text1b(version, 256);
        s.text1b(document_archive_id, 1024);
        s.container(compatible_document_archive_ids, 256,
                    [](S& s, std::string& id) { s.text1b(id, 1024); });
        s.container(analyzeable_content_types, 256,
                    [](S& s, ARA::ARAContentType& content_type) {
                        s.value4b(content_type);
                    });
        s.value4b(supported_playback_transformation_flags);
        s.value4b(supports_storing_audio_file_chunks);
        s.value4b(supports_sample_based_audio_sources);
        s.value4b(supports_content_only_audio_sources);
        s.value4b(requires_preset_audio_sources);
    }
};

/**
 * Serializable subset of `ARA::ARAInterfaceConfiguration`. The assert function
 * address is not forwarded across the bridge.
 */
struct YaARAFactoryConfig {
    YaARAFactoryConfig() noexcept;
    explicit YaARAFactoryConfig(
        const ARA::ARAInterfaceConfiguration* config) noexcept;

    bool has_config = false;
    ARA::ARASize struct_size = 0;
    ARA::ARAAPIGeneration desired_api_generation =
        static_cast<ARA::ARAAPIGeneration>(0);
    bool has_assert_function = false;

    template <typename S>
    void serialize(S& s) {
        s.value1b(has_config);
        s.value8b(struct_size);
        s.value4b(desired_api_generation);
        s.value1b(has_assert_function);
    }
};

/**
 * Messages for forwarding `ARA::ARAFactory` calls to the Wine host.
 */
struct YaARAFactory {
    struct Initialize {
        using Response = Ack;

        native_size_t instance_id;
        YaARAFactoryConfig config;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.object(config);
        }
    };

    struct Uninitialize {
        using Response = Ack;

        native_size_t instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
        }
    };
};

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

    /**
     * Message to pass through a call to `IPlugInEntryPoint::getFactory()`.
     */
    struct GetFactoryResponse {
        bool supported = false;
        YaARAFactorySnapshot factory;

        template <typename S>
        void serialize(S& s) {
            s.value1b(supported);
            s.object(factory);
        }
    };

    struct GetFactory {
        using Response = GetFactoryResponse;

        native_size_t instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
        }
    };

    /**
     * Message to pass through a call to
     * `IPlugInEntryPoint::bindToDocumentController()`.
     */
    struct BindToDocumentController {
        using Response = PrimitiveResponse<native_size_t>;

        native_size_t instance_id;
        native_size_t document_controller_ref;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value8b(document_controller_ref);
        }
    };

    // ARA::IPlugInEntryPoint pure virtual methods — not yet implemented.
    // These must be declared to satisfy the abstract base class, but they will
    // never be called because `queryInterface()` in `plugin-proxy.cpp` will
    // not return this interface until it is fully proxied.
    virtual const ARA::ARAFactory* PLUGIN_API getFactory() override = 0;
    ARA_DEPRECATED(2_0_Draft)
    virtual const ARA::ARAPlugInExtensionInstance* PLUGIN_API
    bindToDocumentController(
        ARA::ARADocumentControllerRef documentControllerRef) override = 0;

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

    /**
     * Message to pass through a call to
     * `IPlugInEntryPoint2::bindToDocumentControllerWithRoles()`.
     */
    struct BindToDocumentControllerWithRoles {
        using Response = PrimitiveResponse<native_size_t>;

        native_size_t instance_id;
        native_size_t document_controller_ref;
        ARA::ARAPlugInInstanceRoleFlags known_roles;
        ARA::ARAPlugInInstanceRoleFlags assigned_roles;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value8b(document_controller_ref);
            s.value4b(known_roles);
            s.value4b(assigned_roles);
        }
    };

    // ARA::IPlugInEntryPoint2 pure virtual method — not yet implemented.
    virtual const ARA::ARAPlugInExtensionInstance* PLUGIN_API
    bindToDocumentControllerWithRoles(
        ARA::ARADocumentControllerRef documentControllerRef,
        ARA::ARAPlugInInstanceRoleFlags knownRoles,
        ARA::ARAPlugInInstanceRoleFlags assignedRoles) override = 0;

   protected:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop
