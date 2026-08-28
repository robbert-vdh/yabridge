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

#ifdef WITH_ARA

#include <algorithm>
#include <cassert>

#include "ara-factory.h"

// ---------------------------------------------------------------------------
// Static registry
// ---------------------------------------------------------------------------

std::unordered_map<std::string, YaAraFactory::RegistryEntry>&
YaAraFactory::registry() {
    static std::unordered_map<std::string, RegistryEntry> r;
    return r;
}

std::mutex& YaAraFactory::registry_mutex() {
    static std::mutex m;
    return m;
}

// ---------------------------------------------------------------------------
// Process-wide DC ID counter and per-factory trampoline pool
// ---------------------------------------------------------------------------

static std::atomic<native_size_t> g_next_dc_id{1};

static bool g_slot_used[8] = {};

// Each trampoline looks up registry entry by slot index.
// Defined as a template to generate 8 distinct function addresses.
template <int N>
static const ARA::ARADocumentControllerInstance* ARA_CALL
slot_trampoline(const ARA::ARADocumentControllerHostInstance* hostInstance,
                const ARA::ARADocumentProperties* properties) {
    AraCreateDcFn callback;
    const native_size_t dc_id =
        g_next_dc_id.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard lock(YaAraFactory::registry_mutex());
        auto& reg = YaAraFactory::registry();
        for (auto& [key, entry] : reg) {
            if (entry.slot == N) {
                callback = entry.create_dc;
                break;
            }
        }
    }
    if (!callback)
        return nullptr;
    return callback(hostInstance, properties, dc_id);
}

using TrampFn = const ARA::ARADocumentControllerInstance* (ARA_CALL*)(
    const ARA::ARADocumentControllerHostInstance*,
    const ARA::ARADocumentProperties*);
static const TrampFn g_trampolines[8] = {
    slot_trampoline<0>,
    slot_trampoline<1>,
    slot_trampoline<2>,
    slot_trampoline<3>,
    slot_trampoline<4>,
    slot_trampoline<5>,
    slot_trampoline<6>,
    slot_trampoline<7>,
};

// ---------------------------------------------------------------------------
// fill_factory_fields
// ---------------------------------------------------------------------------

void YaAraFactory::fill_factory_fields(ARA::ARAFactory& factory) const {
    if (!fill_called_) {
        fill_called_ = true;

        compatible_archive_id_ptrs_.clear();
        compatible_archive_id_ptrs_.reserve(compatibleDocumentArchiveIDs.size());
        for (const auto& id : compatibleDocumentArchiveIDs)
            compatible_archive_id_ptrs_.push_back(id.c_str());

        content_type_ptrs_.resize(analyzeableContentTypes.size());
        for (size_t i = 0; i < analyzeableContentTypes.size(); ++i)
            content_type_ptrs_[i] =
                static_cast<ARA::ARAContentType>(analyzeableContentTypes[i]);
    }

    factory.structSize = std::min(static_cast<ARA::ARASize>(structSize),
                                  sizeof(ARA::ARAFactory));
    factory.lowestSupportedApiGeneration =
        static_cast<ARA::ARAAPIGeneration>(lowestSupportedApiGeneration);
    factory.highestSupportedApiGeneration =
        static_cast<ARA::ARAAPIGeneration>(highestSupportedApiGeneration);
    factory.factoryID = factoryID.c_str();
    factory.initializeARAWithConfiguration = stub_initialize;
    factory.uninitializeARA = stub_uninitialize;
    factory.plugInName = plugInName.c_str();
    factory.manufacturerName = manufacturerName.c_str();
    factory.informationURL = informationURL.c_str();
    factory.version = version.c_str();
    factory.documentArchiveID = documentArchiveID.c_str();
    factory.compatibleDocumentArchiveIDsCount =
        static_cast<ARA::ARASize>(compatible_archive_id_ptrs_.size());
    factory.compatibleDocumentArchiveIDs =
        compatible_archive_id_ptrs_.empty() ? nullptr
                                            : compatible_archive_id_ptrs_.data();
    factory.analyzeableContentTypesCount =
        static_cast<ARA::ARASize>(analyzeableContentTypes.size());
    factory.analyzeableContentTypes =
        content_type_ptrs_.empty() ? nullptr : content_type_ptrs_.data();
    factory.supportedPlaybackTransformationFlags =
        static_cast<ARA::ARAPlaybackTransformationFlags>(
            supportedPlaybackTransformationFlags);
    factory.supportsStoringAudioFileChunks =
        static_cast<ARA::ARABool>(supportsStoringAudioFileChunks);
}

// ---------------------------------------------------------------------------
// to_ara_factory
// ---------------------------------------------------------------------------

ARA::ARAFactory YaAraFactory::to_ara_factory(
    AraCreateDcFn create_dc) const {
    ARA::ARAFactory factory{};
    fill_factory_fields(factory);

    if (create_dc) {
        std::lock_guard lock(registry_mutex());
        int slot = -1;
        for (int i = 0; i < 8; ++i) {
            if (!g_slot_used[i]) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            fprintf(stderr,
                    "yabridge: ARA factory trampoline pool exhausted "
                    "(all 8 slots in use)\n");
            factory.createDocumentControllerWithDocument =
                stub_create_document_controller;
            return factory;
        }
        g_slot_used[slot] = true;
        factory.createDocumentControllerWithDocument = g_trampolines[slot];

        auto& entry = registry()[factoryID];
        entry.create_dc = std::move(create_dc);
        entry.slot = slot;
        registered_ = true;
    } else {
        factory.createDocumentControllerWithDocument =
            stub_create_document_controller;
    }

    return factory;
}

// ---------------------------------------------------------------------------
// unregister_factory
// ---------------------------------------------------------------------------

void YaAraFactory::unregister_factory() const noexcept {
    if (!registered_)
        return;
    std::lock_guard lock(registry_mutex());
    auto it = registry().find(factoryID);
    if (it != registry().end()) {
        if (it->second.slot >= 0 && it->second.slot < 8)
            g_slot_used[it->second.slot] = false;
        registry().erase(it);
    }
    registered_ = false;
}

// ---------------------------------------------------------------------------
// from_ara_factory
// ---------------------------------------------------------------------------

YaAraFactory from_ara_factory(const ARA::ARAFactory* factory) {
    if (!factory)
        return YaAraFactory{};

    YaAraFactory result{};

    result.structSize = static_cast<uint64_t>(factory->structSize);
    result.lowestSupportedApiGeneration =
        static_cast<int32_t>(factory->lowestSupportedApiGeneration);
    result.highestSupportedApiGeneration =
        static_cast<int32_t>(factory->highestSupportedApiGeneration);
    result.factoryID = factory->factoryID ? factory->factoryID : "";
    result.plugInName = factory->plugInName ? factory->plugInName : "";
    result.manufacturerName =
        factory->manufacturerName ? factory->manufacturerName : "";
    result.informationURL =
        factory->informationURL ? factory->informationURL : "";
    result.version = factory->version ? factory->version : "";

    if (ARA_IMPLEMENTS_FIELD(factory, ARAFactory, documentArchiveID))
        result.documentArchiveID =
            factory->documentArchiveID ? factory->documentArchiveID : "";

    if (ARA_IMPLEMENTS_FIELD(factory, ARAFactory, compatibleDocumentArchiveIDsCount) &&
        ARA_IMPLEMENTS_FIELD(factory, ARAFactory, compatibleDocumentArchiveIDs) &&
        factory->compatibleDocumentArchiveIDs) {
        const ARA::ARASize limit =
            std::min(factory->compatibleDocumentArchiveIDsCount,
                     static_cast<ARA::ARASize>(256));
        if (factory->compatibleDocumentArchiveIDsCount > limit) {
            fprintf(stderr,
                    "yabridge: ARA factory reports %u compatibleDocumentArchiveIDs, "
                    "capped at 256\n",
                    static_cast<unsigned int>(factory->compatibleDocumentArchiveIDsCount));
        }
        result.compatibleDocumentArchiveIDs.reserve(limit);
        for (ARA::ARASize i = 0; i < limit; ++i) {
            result.compatibleDocumentArchiveIDs.push_back(
                factory->compatibleDocumentArchiveIDs[i]
                    ? factory->compatibleDocumentArchiveIDs[i]
                    : "");
        }
    }

    if (ARA_IMPLEMENTS_FIELD(factory, ARAFactory, analyzeableContentTypesCount) &&
        ARA_IMPLEMENTS_FIELD(factory, ARAFactory, analyzeableContentTypes) &&
        factory->analyzeableContentTypes) {
        const ARA::ARASize limit =
            std::min(factory->analyzeableContentTypesCount,
                     static_cast<ARA::ARASize>(256));
        if (factory->analyzeableContentTypesCount > limit) {
            fprintf(stderr,
                    "yabridge: ARA factory reports %u analyzeableContentTypes, "
                    "capped at 256\n",
                    static_cast<unsigned int>(factory->analyzeableContentTypesCount));
        }
        result.analyzeableContentTypes.reserve(limit);
        for (ARA::ARASize i = 0; i < limit; ++i) {
            result.analyzeableContentTypes.push_back(
                static_cast<int32_t>(factory->analyzeableContentTypes[i]));
        }
    }

    if (ARA_IMPLEMENTS_FIELD(factory, ARAFactory, supportedPlaybackTransformationFlags))
        result.supportedPlaybackTransformationFlags =
            static_cast<int32_t>(factory->supportedPlaybackTransformationFlags);

    if (ARA_IMPLEMENTS_FIELD(factory, ARAFactory, supportsStoringAudioFileChunks))
        result.supportsStoringAudioFileChunks =
            static_cast<int32_t>(factory->supportsStoringAudioFileChunks);

    return result;
}

#endif  // WITH_ARA
