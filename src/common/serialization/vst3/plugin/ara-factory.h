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

#ifdef WITH_ARA

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <ARAInterface.h>

#include "../../common.h"

// Callback invoked when the host calls ARAFactory::createDocumentControllerWithDocument.
// Receives the host instance, document properties, and a freshly allocated ara_dc_id.
using AraCreateDcFn =
    std::function<const ARA::ARADocumentControllerInstance*(
        const ARA::ARADocumentControllerHostInstance*,
        const ARA::ARADocumentProperties*,
        native_size_t ara_dc_id)>;

struct YaAraFactory {
    int32_t lowestSupportedApiGeneration = 0;
    int32_t highestSupportedApiGeneration = 0;
    uint64_t structSize = 0;
    int32_t supportedPlaybackTransformationFlags = 0;
    int32_t supportsStoringAudioFileChunks = 0;

    std::string factoryID;
    std::string plugInName;
    std::string manufacturerName;
    std::string informationURL;
    std::string version;
    std::string documentArchiveID;

    std::vector<std::string> compatibleDocumentArchiveIDs;
    std::vector<int32_t> analyzeableContentTypes;

    template <typename S>
    void serialize(S& s) {
        s.value4b(lowestSupportedApiGeneration);
        s.value4b(highestSupportedApiGeneration);
        s.value8b(structSize);
        s.value4b(supportedPlaybackTransformationFlags);
        s.value4b(supportsStoringAudioFileChunks);
        s.text1b(factoryID, 4096);
        s.text1b(plugInName, 4096);
        s.text1b(manufacturerName, 4096);
        s.text1b(informationURL, 4096);
        s.text1b(version, 4096);
        s.text1b(documentArchiveID, 4096);
        s.container(compatibleDocumentArchiveIDs, 256,
                    [](S& s, std::string& id) { s.text1b(id, 4096); });
        s.container4b(analyzeableContentTypes, 256);
    }

    // Build an ARAFactory C struct whose const char* pointers are backed by
    // this YaAraFactory's string members. Both this object and the returned
    // factory must remain at a stable address for the host's lifetime.
    //
    // If create_dc is provided, registers it in the global factory registry so
    // the createDocumentControllerWithDocument trampoline can route to the
    // bridge. Call unregister_factory() when the owning object is destroyed.
    //
    // When create_dc is null, a stub returning nullptr is installed instead.

    // Registry entry: create_dc callback + assigned trampoline slot index.
    struct RegistryEntry {
        AraCreateDcFn create_dc;
        int slot = -1;
        RegistryEntry() = default;
        explicit RegistryEntry(AraCreateDcFn dc) : create_dc(std::move(dc)) {}
        RegistryEntry(RegistryEntry&&) = default;
        RegistryEntry& operator=(RegistryEntry&&) = default;
        RegistryEntry(const RegistryEntry&) = delete;
        RegistryEntry& operator=(const RegistryEntry&) = delete;
    };

    ARA::ARAFactory to_ara_factory(AraCreateDcFn create_dc = nullptr) const;

    // Remove the factory from the global registry. Must be called when the
    // owning object is destroyed, if to_ara_factory was called with a non-null
    // create_dc.
    void unregister_factory() const noexcept;

    // Accessible from the file-scope slot trampolines in ara-factory.cpp.
    static std::unordered_map<std::string, RegistryEntry>& registry();
    static std::mutex& registry_mutex();

   private:
    static void ARA_CALL stub_initialize(
        const ARA::ARAInterfaceConfiguration*) {}
    static void ARA_CALL stub_uninitialize() {}

    static const ARA::ARADocumentControllerInstance* ARA_CALL
    stub_create_document_controller(
        const ARA::ARADocumentControllerHostInstance*,
        const ARA::ARADocumentProperties*) {
        return nullptr;
    }

    void fill_factory_fields(ARA::ARAFactory& factory) const;

    mutable std::vector<const char*> compatible_archive_id_ptrs_;
    mutable std::vector<ARA::ARAContentType> content_type_ptrs_;
    mutable bool registered_ = false;
    mutable bool fill_called_ = false;
};

YaAraFactory from_ara_factory(const ARA::ARAFactory* factory);

struct YaAraPlugInExtensionInstance {
    bool has_playback_renderer = false;
    bool has_editor_renderer = false;
    bool has_editor_view = false;

    // Opaque renderer refs from the Wine plugin; sent back with every
    // renderer interface call so the Wine side can route to the right object.
    uint64_t playback_renderer_ref = 0;
    uint64_t editor_renderer_ref = 0;
    uint64_t editor_view_ref = 0;

    template <typename S>
    void serialize(S& s) {
        s.value1b(has_playback_renderer);
        s.value1b(has_editor_renderer);
        s.value1b(has_editor_view);
        s.value8b(playback_renderer_ref);
        s.value8b(editor_renderer_ref);
        s.value8b(editor_view_ref);
    }
};

#endif  // WITH_ARA
