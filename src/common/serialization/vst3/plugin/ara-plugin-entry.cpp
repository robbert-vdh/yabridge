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

#include "ara-plugin-entry.h"

#include "../../../logging/common.h"

namespace ARA {
DEF_CLASS_IID(IPlugInEntryPoint)
DEF_CLASS_IID(IPlugInEntryPoint2)
}  // namespace ARA

YaARAFactorySnapshot::YaARAFactorySnapshot() noexcept = default;

YaARAFactorySnapshot::YaARAFactorySnapshot(
    const ARA::ARAFactory* factory) noexcept {
    if (!factory) {
        return;
    }

    struct_size = static_cast<uint64_t>(factory->structSize);
    lowest_supported_api_generation = factory->lowestSupportedApiGeneration;
    highest_supported_api_generation = factory->highestSupportedApiGeneration;

    if (factory->factoryID) {
        factory_id = factory->factoryID;
    }
    if (factory->plugInName) {
        plug_in_name = factory->plugInName;
    }
    if (factory->manufacturerName) {
        manufacturer_name = factory->manufacturerName;
    }
    if (factory->informationURL) {
        information_url = factory->informationURL;
    }
    if (factory->version) {
        version = factory->version;
    }

    if (factory->documentArchiveID) {
        document_archive_id = factory->documentArchiveID;
    }
    if (factory->compatibleDocumentArchiveIDsCount > 0 &&
        factory->compatibleDocumentArchiveIDs) {
        compatible_document_archive_ids.reserve(
            static_cast<size_t>(factory->compatibleDocumentArchiveIDsCount));
        for (ARA::ARASize i = 0;
             i < factory->compatibleDocumentArchiveIDsCount;
             ++i) {
            const ARA::ARAPersistentID id =
                factory->compatibleDocumentArchiveIDs[i];
            compatible_document_archive_ids.emplace_back(id ? id : "");
        }
    }

    if (factory->analyzeableContentTypesCount > 0 &&
        factory->analyzeableContentTypes) {
        analyzeable_content_types.reserve(
            static_cast<size_t>(factory->analyzeableContentTypesCount));
           for (ARA::ARASize i = 0;
               i < factory->analyzeableContentTypesCount;
               ++i) {
            analyzeable_content_types.push_back(
                factory->analyzeableContentTypes[i]);
        }
    }

    supported_playback_transformation_flags =
        factory->supportedPlaybackTransformationFlags;
    supports_storing_audio_file_chunks = factory->supportsStoringAudioFileChunks;
    supports_sample_based_audio_sources =
        factory->supportsSampleBasedAudioSources;
    supports_content_only_audio_sources =
        factory->supportsContentOnlyAudioSources;
    requires_preset_audio_sources = factory->requiresPresetAudioSources;
}

YaARAFactoryConfig::YaARAFactoryConfig() noexcept = default;

YaARAFactoryConfig::YaARAFactoryConfig(
    const ARA::ARAInterfaceConfiguration* config) noexcept {
    if (!config) {
        return;
    }

    has_config = true;
    struct_size = static_cast<uint64_t>(config->structSize);
    desired_api_generation = config->desiredApiGeneration;
    has_assert_function = config->assertFunctionAddress != nullptr;
}

YaARAPlugInEntryPoint::ConstructArgs::ConstructArgs() noexcept : supported(false) {}

YaARAPlugInEntryPoint::ConstructArgs::ConstructArgs(
    Steinberg::IPtr<Steinberg::FUnknown> object) noexcept
    : supported(Steinberg::FUnknownPtr<ARA::IPlugInEntryPoint>(object)) {
    if (supported) {
        Logger::create_exception_logger().log(
            "DETECTED ARA PLUGIN CAPABILITY (ARA::IPlugInEntryPoint)!");
    }
}

YaARAPlugInEntryPoint::YaARAPlugInEntryPoint(ConstructArgs&& args) noexcept
    : arguments_(std::move(args)) {}

YaARAPlugInEntryPoint2::ConstructArgs::ConstructArgs() noexcept : supported(false) {}

YaARAPlugInEntryPoint2::ConstructArgs::ConstructArgs(
    Steinberg::IPtr<Steinberg::FUnknown> object) noexcept
    : supported(Steinberg::FUnknownPtr<ARA::IPlugInEntryPoint2>(object)) {
    if (supported) {
        Logger::create_exception_logger().log(
            "DETECTED ARA2 PLUGIN CAPABILITY (ARA::IPlugInEntryPoint2)!");
    }
}

YaARAPlugInEntryPoint2::YaARAPlugInEntryPoint2(ConstructArgs&& args) noexcept
    : arguments_(std::move(args)) {}
