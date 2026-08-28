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

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <ARAInterface.h>

#include "../../bitsery/ext/in-place-optional.h"
#include "../../bitsery/ext/in-place-variant.h"
#include "../../audio-shm.h"
#include "../common.h"
#include "base.h"
#include "plugin/ara-factory.h"

// Serializable ARA properties structs. All ARA*HostRef values that cross IPC
// boundaries are encoded as uint64_t handles. Plugin-side refs (ARA*Ref) are
// similarly encoded as uint64_t. All messages carry ara_dc_id to identify the
// document controller.

// ---------------------------------------------------------------------------
// Serializable property structs
// ---------------------------------------------------------------------------

struct YaAraColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;

    template <typename S>
    void serialize(S& s) {
        s.value4b(r);
        s.value4b(g);
        s.value4b(b);
    }
};

struct YaAraDocumentProperties {
    std::string name;

    template <typename S>
    void serialize(S& s) {
        s.text1b(name, 4096);
    }
};

struct YaAraMusicalContextProperties {
    std::optional<std::string> name;
    int32_t order_index = 0;
    std::optional<YaAraColor> color;

    template <typename S>
    void serialize(S& s) {
        s.ext(name, bitsery::ext::InPlaceOptional{},
              [](S& s, std::string& v) { s.text1b(v, 4096); });
        s.value4b(order_index);
        s.ext(color, bitsery::ext::InPlaceOptional{},
              [](S& s, YaAraColor& v) { s.object(v); });
    }
};

struct YaAraRegionSequenceProperties {
    std::optional<std::string> name;
    int32_t order_index = 0;
    uint64_t musical_context_ref = 0;
    std::optional<YaAraColor> color;

    template <typename S>
    void serialize(S& s) {
        s.ext(name, bitsery::ext::InPlaceOptional{},
              [](S& s, std::string& v) { s.text1b(v, 4096); });
        s.value4b(order_index);
        s.value8b(musical_context_ref);
        s.ext(color, bitsery::ext::InPlaceOptional{},
              [](S& s, YaAraColor& v) { s.object(v); });
    }
};

struct YaAraSampleChannelArrangement {
    int32_t data_type = 0;
    std::vector<uint8_t> data;

    template <typename S>
    void serialize(S& s) {
        s.value4b(data_type);
        s.container1b(data, 65536);
    }
};

struct YaAraAudioSourceProperties {
    std::optional<std::string> name;
    std::string persistent_id;
    int64_t sample_count = 0;
    double sample_rate = 0.0;
    int32_t channel_count = 0;
    int32_t merits_64bit_samples = 0;
    std::optional<YaAraSampleChannelArrangement> channel_arrangement;

    template <typename S>
    void serialize(S& s) {
        s.ext(name, bitsery::ext::InPlaceOptional{},
              [](S& s, std::string& v) { s.text1b(v, 4096); });
        s.text1b(persistent_id, 4096);
        s.value8b(sample_count);
        s.value8b(sample_rate);
        s.value4b(channel_count);
        s.value4b(merits_64bit_samples);
        s.ext(channel_arrangement, bitsery::ext::InPlaceOptional{},
              [](S& s, YaAraSampleChannelArrangement& v) { s.object(v); });
    }
};

struct YaAraAudioModificationProperties {
    std::optional<std::string> name;
    std::string persistent_id;

    template <typename S>
    void serialize(S& s) {
        s.ext(name, bitsery::ext::InPlaceOptional{},
              [](S& s, std::string& v) { s.text1b(v, 4096); });
        s.text1b(persistent_id, 4096);
    }
};

struct YaAraPlaybackRegionProperties {
    int32_t transformation_flags = 0;
    double start_in_modification_time = 0.0;
    double duration_in_modification_time = 0.0;
    double start_in_playback_time = 0.0;
    double duration_in_playback_time = 0.0;
    // ARA1 deprecated field, still transmitted for compatibility
    uint64_t musical_context_ref = 0;
    uint64_t region_sequence_ref = 0;
    std::optional<std::string> name;
    std::optional<YaAraColor> color;

    template <typename S>
    void serialize(S& s) {
        s.value4b(transformation_flags);
        s.value8b(start_in_modification_time);
        s.value8b(duration_in_modification_time);
        s.value8b(start_in_playback_time);
        s.value8b(duration_in_playback_time);
        s.value8b(musical_context_ref);
        s.value8b(region_sequence_ref);
        s.ext(name, bitsery::ext::InPlaceOptional{},
              [](S& s, std::string& v) { s.text1b(v, 4096); });
        s.ext(color, bitsery::ext::InPlaceOptional{},
              [](S& s, YaAraColor& v) { s.object(v); });
    }
};

struct YaAraContentTimeRange {
    double start = 0.0;
    double duration = 0.0;

    template <typename S>
    void serialize(S& s) {
        s.value8b(start);
        s.value8b(duration);
    }
};

struct YaAraRestoreObjectsFilter {
    int32_t document_data = 0;
    std::vector<std::string> audio_source_archive_ids;
    std::vector<std::string> audio_source_current_ids;
    std::vector<std::string> audio_modification_archive_ids;
    std::vector<std::string> audio_modification_current_ids;

    template <typename S>
    void serialize(S& s) {
        s.value4b(document_data);
        s.container(audio_source_archive_ids, 65536,
                    [](S& s, std::string& v) { s.text1b(v, 4096); });
        s.container(audio_source_current_ids, 65536,
                    [](S& s, std::string& v) { s.text1b(v, 4096); });
        s.container(audio_modification_archive_ids, 65536,
                    [](S& s, std::string& v) { s.text1b(v, 4096); });
        s.container(audio_modification_current_ids, 65536,
                    [](S& s, std::string& v) { s.text1b(v, 4096); });
    }
};

struct YaAraStoreObjectsFilter {
    int32_t document_data = 0;
    std::vector<uint64_t> audio_source_refs;
    std::vector<uint64_t> audio_modification_refs;

    template <typename S>
    void serialize(S& s) {
        s.value4b(document_data);
        s.container8b(audio_source_refs, 65536);
        s.container8b(audio_modification_refs, 65536);
    }
};

// ---------------------------------------------------------------------------
// Message structs for document controller IPC
// ---------------------------------------------------------------------------

namespace YaAra {

// -- Shared response wrapper types -------------------------------------------

struct Int32Response {
    int32_t value = 0;
    template <typename S>
    void serialize(S& s) { s.value4b(value); }
};

struct BytesResponse {
    std::vector<uint8_t> data;
    template <typename S>
    void serialize(S& s) { s.container1b(data, 1 << 26); }
};

struct HandleResponse {
    uint64_t value = 0;
    template <typename S>
    void serialize(S& s) { s.value8b(value); }
};

// -- Lifecycle ---------------------------------------------------------------

struct CreateDocumentController {
    using Response = std::variant<uint64_t, UniversalTResult>;

    native_size_t instance_id;
    native_size_t ara_dc_id;
    std::string factory_id;
    YaAraDocumentProperties document_properties;

    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value8b(ara_dc_id);
        s.text1b(factory_id, 4096);
        s.object(document_properties);
    }
};

struct DestroyDocumentController {
    using Response = Ack;

    native_size_t ara_dc_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
    }
};

// -- Document editing --------------------------------------------------------

struct BeginEditing {
    using Response = Ack;

    native_size_t ara_dc_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
    }
};

struct EndEditing {
    using Response = Ack;

    native_size_t ara_dc_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
    }
};

struct NotifyModelUpdates {
    using Response = Ack;

    native_size_t ara_dc_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
    }
};

struct UpdateDocumentProperties {
    using Response = Ack;

    native_size_t ara_dc_id;
    YaAraDocumentProperties properties;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.object(properties);
    }
};

// -- Musical context ---------------------------------------------------------

struct AddMusicalContext {
    using Response = std::variant<uint64_t, UniversalTResult>;

    native_size_t ara_dc_id;
    uint64_t host_ref;
    YaAraMusicalContextProperties properties;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(host_ref);
        s.object(properties);
    }
};

struct UpdateMusicalContextProperties {
    using Response = Ack;

    native_size_t ara_dc_id;
    uint64_t musical_context_ref;
    YaAraMusicalContextProperties properties;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(musical_context_ref);
        s.object(properties);
    }
};

struct UpdateMusicalContextContent {
    using Response = Ack;

    native_size_t ara_dc_id;
    uint64_t musical_context_ref;
    std::optional<YaAraContentTimeRange> range;
    int32_t flags;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(musical_context_ref);
        s.ext(range, bitsery::ext::InPlaceOptional{},
              [](S& s, YaAraContentTimeRange& v) { s.object(v); });
        s.value4b(flags);
    }
};

struct RemoveMusicalContext {
    using Response = Ack;

    native_size_t ara_dc_id;
    uint64_t musical_context_ref;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(musical_context_ref);
    }
};

// -- Region sequences --------------------------------------------------------

struct AddRegionSequence {
    using Response = std::variant<uint64_t, UniversalTResult>;

    native_size_t ara_dc_id;
    uint64_t host_ref;
    YaAraRegionSequenceProperties properties;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(host_ref);
        s.object(properties);
    }
};

struct UpdateRegionSequenceProperties {
    using Response = Ack;

    native_size_t ara_dc_id;
    uint64_t region_sequence_ref;
    YaAraRegionSequenceProperties properties;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(region_sequence_ref);
        s.object(properties);
    }
};

struct RemoveRegionSequence {
    using Response = Ack;

    native_size_t ara_dc_id;
    uint64_t region_sequence_ref;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(region_sequence_ref);
    }
};

// -- Audio source ------------------------------------------------------------

struct AddAudioSource {
    using Response = std::variant<uint64_t, UniversalTResult>;

    native_size_t ara_dc_id;
    uint64_t host_ref;
    YaAraAudioSourceProperties properties;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(host_ref);
        s.object(properties);
    }
};

struct UpdateAudioSourceProperties {
    using Response = Ack;

    native_size_t ara_dc_id;
    uint64_t audio_source_ref;
    YaAraAudioSourceProperties properties;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_source_ref);
        s.object(properties);
    }
};

struct UpdateAudioSourceContent {
    using Response = Ack;

    native_size_t ara_dc_id;
    uint64_t audio_source_ref;
    std::optional<YaAraContentTimeRange> range;
    int32_t flags;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_source_ref);
        s.ext(range, bitsery::ext::InPlaceOptional{},
              [](S& s, YaAraContentTimeRange& v) { s.object(v); });
        s.value4b(flags);
    }
};

struct EnableAudioSourceSamplesAccess {
    using Response = Ack;

    native_size_t ara_dc_id;
    uint64_t audio_source_ref;
    int32_t enable;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_source_ref);
        s.value4b(enable);
    }
};

struct DeactivateAndUnregisterAudioSource {
    using Response = Ack;

    native_size_t ara_dc_id;
    uint64_t audio_source_ref;
    int32_t deactivate;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_source_ref);
        s.value4b(deactivate);
    }
};

struct RemoveAudioSource {
    using Response = Ack;

    native_size_t ara_dc_id;
    uint64_t audio_source_ref;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_source_ref);
    }
};

// -- Audio modification ------------------------------------------------------

struct AddAudioModification {
    using Response = std::variant<uint64_t, UniversalTResult>;

    native_size_t ara_dc_id;
    uint64_t audio_source_ref;
    uint64_t host_ref;
    YaAraAudioModificationProperties properties;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_source_ref);
        s.value8b(host_ref);
        s.object(properties);
    }
};

struct CloneAudioModification {
    using Response = std::variant<uint64_t, UniversalTResult>;

    native_size_t ara_dc_id;
    uint64_t audio_modification_ref;
    uint64_t host_ref;
    YaAraAudioModificationProperties properties;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_modification_ref);
        s.value8b(host_ref);
        s.object(properties);
    }
};

struct UpdateAudioModificationProperties {
    using Response = Ack;

    native_size_t ara_dc_id;
    uint64_t audio_modification_ref;
    YaAraAudioModificationProperties properties;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_modification_ref);
        s.object(properties);
    }
};

struct DeactivateAndUnregisterAudioModification {
    using Response = Ack;

    native_size_t ara_dc_id;
    uint64_t audio_modification_ref;
    int32_t deactivate;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_modification_ref);
        s.value4b(deactivate);
    }
};

struct RemoveAudioModification {
    using Response = Ack;

    native_size_t ara_dc_id;
    uint64_t audio_modification_ref;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_modification_ref);
    }
};

// -- Playback region ---------------------------------------------------------

struct AddPlaybackRegion {
    using Response = std::variant<uint64_t, UniversalTResult>;

    native_size_t ara_dc_id;
    uint64_t audio_modification_ref;
    uint64_t host_ref;
    YaAraPlaybackRegionProperties properties;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_modification_ref);
        s.value8b(host_ref);
        s.object(properties);
    }
};

struct UpdatePlaybackRegionProperties {
    using Response = Ack;

    native_size_t ara_dc_id;
    uint64_t playback_region_ref;
    YaAraPlaybackRegionProperties properties;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(playback_region_ref);
        s.object(properties);
    }
};

struct RemovePlaybackRegion {
    using Response = Ack;

    native_size_t ara_dc_id;
    uint64_t playback_region_ref;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(playback_region_ref);
    }
};

// -- Audio sample transfer ---------------------------------------------------

struct CreateAudioReader {
    struct Response {
        uint64_t reader_id = 0;
        AudioShmBuffer::Config shm_config;

        template <typename S>
        void serialize(S& s) {
            s.value8b(reader_id);
            s.object(shm_config);
        }
    };

    native_size_t ara_dc_id;
    uint64_t audio_source_host_ref;
    bool use_64bit;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_source_host_ref);
        s.value1b(use_64bit);
    }
};

struct DestroyAudioReader {
    using Response = Ack;

    native_size_t ara_dc_id;
    uint64_t audio_reader_id;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_reader_id);
    }
};

struct ReadAudioSamples {
    struct Response {
        bool success = false;
        template <typename S>
        void serialize(S& s) {
            s.value1b(success);
        }
    };

    native_size_t ara_dc_id;
    uint64_t audio_reader_host_ref;
    int64_t sample_position;
    int32_t sample_count;
    bool use_64bit;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_reader_host_ref);
        s.value8b(sample_position);
        s.value4b(sample_count);
        s.value1b(use_64bit);
    }
};

// -- Content / analysis ------------------------------------------------------

struct RequestAudioSourceContentAnalysis {
    using Response = Ack;

    native_size_t ara_dc_id;
    uint64_t audio_source_ref;
    std::vector<int32_t> content_types;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_source_ref);
        s.container4b(content_types, 256);
    }
};

struct GetPlaybackRegionHeadAndTailTime {
    struct Response {
        double head_time = 0.0;
        double tail_time = 0.0;

        template <typename S>
        void serialize(S& s) {
            s.value8b(head_time);
            s.value8b(tail_time);
        }
    };

    native_size_t ara_dc_id;
    uint64_t playback_region_ref;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(playback_region_ref);
    }
};

// -- Content queries (host queries plugin's DC for content) ------------------

struct IsAudioSourceContentAvailableDC {
    using Response = Int32Response;

    native_size_t ara_dc_id;
    uint64_t audio_source_ref;
    int32_t content_type;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_source_ref);
        s.value4b(content_type);
    }
};

struct GetAudioSourceContentGradeDC {
    using Response = Int32Response;

    native_size_t ara_dc_id;
    uint64_t audio_source_ref;
    int32_t content_type;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_source_ref);
        s.value4b(content_type);
    }
};

struct CreateAudioSourceContentReaderDC {
    using Response = Int32Response;

    native_size_t ara_dc_id;
    uint64_t audio_source_ref;
    int32_t content_type;
    std::optional<YaAraContentTimeRange> range;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_source_ref);
        s.value4b(content_type);
        s.ext(range, bitsery::ext::InPlaceOptional{},
              [](S& s, YaAraContentTimeRange& v) { s.object(v); });
    }
};

struct GetContentReaderEventCountDC {
    using Response = Int32Response;

    native_size_t ara_dc_id;
    uint64_t content_reader_ref;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(content_reader_ref);
    }
};

struct GetContentReaderDataForEventDC {
    using Response = BytesResponse;

    native_size_t ara_dc_id;
    uint64_t content_reader_ref;
    int32_t event_index;
    int32_t content_type;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(content_reader_ref);
        s.value4b(event_index);
        s.value4b(content_type);
    }
};

struct DestroyContentReaderDC {
    using Response = Ack;

    native_size_t ara_dc_id;
    uint64_t content_reader_ref;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(content_reader_ref);
    }
};

// -- Archiving ---------------------------------------------------------------

struct StoreObjectsToArchive {
    using Response = std::variant<int32_t, UniversalTResult>;

    native_size_t ara_dc_id;
    uint64_t archive_writer_host_ref;
    std::optional<YaAraStoreObjectsFilter> filter;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(archive_writer_host_ref);
        s.ext(filter, bitsery::ext::InPlaceOptional{},
              [](S& s, YaAraStoreObjectsFilter& v) { s.object(v); });
    }
};

struct RestoreObjectsFromArchive {
    using Response = std::variant<int32_t, UniversalTResult>;

    native_size_t ara_dc_id;
    uint64_t archive_reader_host_ref;
    std::optional<YaAraRestoreObjectsFilter> filter;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(archive_reader_host_ref);
        s.ext(filter, bitsery::ext::InPlaceOptional{},
              [](S& s, YaAraRestoreObjectsFilter& v) { s.object(v); });
    }
};

struct StoreDocumentToArchive {
    using Response = std::variant<int32_t, UniversalTResult>;

    native_size_t ara_dc_id;
    uint64_t archive_writer_host_ref;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(archive_writer_host_ref);
    }
};

struct BeginRestoringDocumentFromArchive {
    using Response = std::variant<int32_t, UniversalTResult>;

    native_size_t ara_dc_id;
    uint64_t archive_reader_host_ref;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(archive_reader_host_ref);
    }
};

struct EndRestoringDocumentFromArchive {
    using Response = std::variant<int32_t, UniversalTResult>;

    native_size_t ara_dc_id;
    uint64_t archive_reader_host_ref;

    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(archive_reader_host_ref);
    }
};

// ---------------------------------------------------------------------------
// Host callback messages (plugin -> host, over plugin_host_callback_ socket)
// ---------------------------------------------------------------------------

namespace HostCallback {

// Alias shared response types from parent namespace.
using HandleResponse = YaAra::HandleResponse;
using Int32Response  = YaAra::Int32Response;
using BytesResponse  = YaAra::BytesResponse;

// Wrapper for string responses (e.g. archive ID)
struct StringResponse {
    std::string value;
    template <typename S>
    void serialize(S& s) {
        s.text1b(value, 4096);
    }
};

struct CreateAudioReader {
    struct Response {
        uint64_t value = 0;
        int32_t channel_count = 0;
        template <typename S>
        void serialize(S& s) {
            s.value8b(value);
            s.value4b(channel_count);
        }
    };
    native_size_t ara_dc_id;
    uint64_t audio_source_host_ref;
    int32_t use_64bit_samples;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_source_host_ref);
        s.value4b(use_64bit_samples);
    }
};

struct DestroyAudioReader {
    using Response = Ack;
    native_size_t ara_dc_id;
    uint64_t audio_reader_id;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_reader_id);
    }
};

struct GetArchiveSize {
    using Response = HandleResponse;
    native_size_t ara_dc_id;
    uint64_t archive_reader_host_ref;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(archive_reader_host_ref);
    }
};

struct ReadBytesFromArchive {
    using Response = BytesResponse;
    native_size_t ara_dc_id;
    uint64_t archive_reader_host_ref;
    uint64_t position;
    uint64_t length;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(archive_reader_host_ref);
        s.value8b(position);
        s.value8b(length);
    }
};

struct WriteBytesToArchive {
    using Response = Int32Response;
    native_size_t ara_dc_id;
    uint64_t archive_writer_host_ref;
    uint64_t position;
    std::vector<uint8_t> data;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(archive_writer_host_ref);
        s.value8b(position);
        s.container1b(data, 67108864);
    }
};

struct NotifyDocumentArchivingProgress {
    using Response = Ack;
    native_size_t ara_dc_id;
    float value;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value4b(value);
    }
};

struct NotifyDocumentUnarchivingProgress {
    using Response = Ack;
    native_size_t ara_dc_id;
    float value;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value4b(value);
    }
};

struct GetDocumentArchiveID {
    using Response = StringResponse;
    native_size_t ara_dc_id;
    uint64_t archive_reader_host_ref;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(archive_reader_host_ref);
    }
};

struct IsMusicalContextContentAvailable {
    using Response = Int32Response;
    native_size_t ara_dc_id;
    uint64_t musical_context_host_ref;
    int32_t content_type;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(musical_context_host_ref);
        s.value4b(content_type);
    }
};

struct GetMusicalContextContentGrade {
    using Response = Int32Response;
    native_size_t ara_dc_id;
    uint64_t musical_context_host_ref;
    int32_t content_type;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(musical_context_host_ref);
        s.value4b(content_type);
    }
};

struct CreateMusicalContextContentReader {
    using Response = HandleResponse;
    native_size_t ara_dc_id;
    uint64_t musical_context_host_ref;
    int32_t content_type;
    std::optional<YaAraContentTimeRange> range;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(musical_context_host_ref);
        s.value4b(content_type);
        s.ext(range, bitsery::ext::InPlaceOptional{},
              [](S& s, YaAraContentTimeRange& v) { s.object(v); });
    }
};

struct IsAudioSourceContentAvailable {
    using Response = Int32Response;
    native_size_t ara_dc_id;
    uint64_t audio_source_host_ref;
    int32_t content_type;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_source_host_ref);
        s.value4b(content_type);
    }
};

struct GetAudioSourceContentGrade {
    using Response = Int32Response;
    native_size_t ara_dc_id;
    uint64_t audio_source_host_ref;
    int32_t content_type;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_source_host_ref);
        s.value4b(content_type);
    }
};

struct CreateAudioSourceContentReader {
    using Response = HandleResponse;
    native_size_t ara_dc_id;
    uint64_t audio_source_host_ref;
    int32_t content_type;
    std::optional<YaAraContentTimeRange> range;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_source_host_ref);
        s.value4b(content_type);
        s.ext(range, bitsery::ext::InPlaceOptional{},
              [](S& s, YaAraContentTimeRange& v) { s.object(v); });
    }
};

struct GetContentReaderEventCount {
    using Response = Int32Response;
    native_size_t ara_dc_id;
    uint64_t content_reader_host_ref;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(content_reader_host_ref);
    }
};

struct GetContentReaderDataForEvent {
    using Response = BytesResponse;
    native_size_t ara_dc_id;
    uint64_t content_reader_host_ref;
    int32_t event_index;
    int32_t content_type;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(content_reader_host_ref);
        s.value4b(event_index);
        s.value4b(content_type);
    }
};

struct DestroyContentReader {
    using Response = Ack;
    native_size_t ara_dc_id;
    uint64_t content_reader_host_ref;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(content_reader_host_ref);
    }
};

struct NotifyAudioSourceAnalysisProgress {
    using Response = Ack;
    native_size_t ara_dc_id;
    uint64_t audio_source_host_ref;
    int32_t state;
    float value;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_source_host_ref);
        s.value4b(state);
        s.value4b(value);
    }
};

struct NotifyAudioSourceContentChanged {
    using Response = Ack;
    native_size_t ara_dc_id;
    uint64_t audio_source_host_ref;
    std::optional<YaAraContentTimeRange> range;
    int32_t flags;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_source_host_ref);
        s.ext(range, bitsery::ext::InPlaceOptional{},
              [](S& s, YaAraContentTimeRange& v) { s.object(v); });
        s.value4b(flags);
    }
};

struct NotifyAudioModificationContentChanged {
    using Response = Ack;
    native_size_t ara_dc_id;
    uint64_t audio_modification_host_ref;
    std::optional<YaAraContentTimeRange> range;
    int32_t flags;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(audio_modification_host_ref);
        s.ext(range, bitsery::ext::InPlaceOptional{},
              [](S& s, YaAraContentTimeRange& v) { s.object(v); });
        s.value4b(flags);
    }
};

struct NotifyPlaybackRegionContentChanged {
    using Response = Ack;
    native_size_t ara_dc_id;
    uint64_t playback_region_host_ref;
    std::optional<YaAraContentTimeRange> range;
    int32_t flags;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(playback_region_host_ref);
        s.ext(range, bitsery::ext::InPlaceOptional{},
              [](S& s, YaAraContentTimeRange& v) { s.object(v); });
        s.value4b(flags);
    }
};

struct NotifyDocumentDataChanged {
    using Response = Ack;
    native_size_t ara_dc_id;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
    }
};

struct RequestStartPlayback {
    using Response = Ack;
    native_size_t ara_dc_id;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
    }
};

struct RequestStopPlayback {
    using Response = Ack;
    native_size_t ara_dc_id;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
    }
};

struct RequestSetPlaybackPosition {
    using Response = Ack;
    native_size_t ara_dc_id;
    double time_position;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(time_position);
    }
};

struct RequestSetCycleRange {
    using Response = Ack;
    native_size_t ara_dc_id;
    double start_time;
    double duration;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value8b(start_time);
        s.value8b(duration);
    }
};

struct RequestEnableCycle {
    using Response = Ack;
    native_size_t ara_dc_id;
    int32_t enable;
    template <typename S>
    void serialize(S& s) {
        s.value8b(ara_dc_id);
        s.value4b(enable);
    }
};

}  // namespace HostCallback

// -- Plugin extension interface calls ----------------------------------------
// These are host -> plugin calls on the ARAPlaybackRendererInterface,
// ARAEditorRendererInterface, and ARAEditorViewInterface. They use
// instance_id to route to the right plugin instance on the Wine side, plus
// the opaque renderer ref returned by bindToDocumentControllerWithRoles.

namespace PluginExtension {

struct PlaybackRendererAddRegion {
    using Response = Ack;
    native_size_t instance_id;
    uint64_t playback_renderer_ref;
    uint64_t playback_region_ref;
    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value8b(playback_renderer_ref);
        s.value8b(playback_region_ref);
    }
};

struct PlaybackRendererRemoveRegion {
    using Response = Ack;
    native_size_t instance_id;
    uint64_t playback_renderer_ref;
    uint64_t playback_region_ref;
    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value8b(playback_renderer_ref);
        s.value8b(playback_region_ref);
    }
};

struct EditorRendererAddRegion {
    using Response = Ack;
    native_size_t instance_id;
    uint64_t editor_renderer_ref;
    uint64_t playback_region_ref;
    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value8b(editor_renderer_ref);
        s.value8b(playback_region_ref);
    }
};

struct EditorRendererRemoveRegion {
    using Response = Ack;
    native_size_t instance_id;
    uint64_t editor_renderer_ref;
    uint64_t playback_region_ref;
    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value8b(editor_renderer_ref);
        s.value8b(playback_region_ref);
    }
};

struct EditorRendererAddRegionSequence {
    using Response = Ack;
    native_size_t instance_id;
    uint64_t editor_renderer_ref;
    uint64_t region_sequence_ref;
    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value8b(editor_renderer_ref);
        s.value8b(region_sequence_ref);
    }
};

struct EditorRendererRemoveRegionSequence {
    using Response = Ack;
    native_size_t instance_id;
    uint64_t editor_renderer_ref;
    uint64_t region_sequence_ref;
    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value8b(editor_renderer_ref);
        s.value8b(region_sequence_ref);
    }
};

struct EditorViewNotifySelection {
    using Response = Ack;
    native_size_t instance_id;
    uint64_t editor_view_ref;
    std::vector<uint64_t> playback_region_refs;
    std::vector<uint64_t> region_sequence_refs;
    std::optional<YaAraContentTimeRange> time_range;
    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value8b(editor_view_ref);
        s.container8b(playback_region_refs, 65536);
        s.container8b(region_sequence_refs, 65536);
        s.ext(time_range, bitsery::ext::InPlaceOptional{},
              [](S& s2, YaAraContentTimeRange& v) { s2.object(v); });
    }
};

struct EditorViewNotifyHideRegionSequences {
    using Response = Ack;
    native_size_t instance_id;
    uint64_t editor_view_ref;
    std::vector<uint64_t> region_sequence_refs;
    template <typename S>
    void serialize(S& s) {
        s.value8b(instance_id);
        s.value8b(editor_view_ref);
        s.container8b(region_sequence_refs, 65536);
    }
};

}  // namespace PluginExtension

}  // namespace YaAra

// ---------------------------------------------------------------------------
// Free serialization functions for response variants
// ---------------------------------------------------------------------------

// native_size_t is uint64_t, so this covers both AddMusicalContext-style
// responses and CreateDocumentController's response.
template <typename S>
void serialize(S& s, std::variant<uint64_t, UniversalTResult>& result) {
    s.ext(result,
          bitsery::ext::InPlaceVariant<bitsery::ext::OverloadValue<uint64_t,
                                                                    8>>{});
}

template <typename S>
void serialize(S& s, std::variant<int32_t, UniversalTResult>& result) {
    s.ext(result,
          bitsery::ext::InPlaceVariant<bitsery::ext::OverloadValue<int32_t,
                                                                    4>>{});
}

#endif  // WITH_ARA
