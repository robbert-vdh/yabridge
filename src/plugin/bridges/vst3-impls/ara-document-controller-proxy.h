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
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <ARAInterface.h>

#include "../../../common/audio-shm.h"
#include "../../../common/serialization/vst3/ara-document-controller.h"

class Vst3PluginBridge;

class AraDocumentControllerProxy {
   public:
    AraDocumentControllerProxy(Vst3PluginBridge& bridge,
                               native_size_t ara_dc_id) noexcept;

    AraDocumentControllerProxy(const AraDocumentControllerProxy&) = delete;
    AraDocumentControllerProxy& operator=(const AraDocumentControllerProxy&) =
        delete;

    const ARA::ARADocumentControllerInstance& ara_dc_instance() const noexcept {
        return ara_dc_instance_;
    }

    native_size_t ara_dc_id() const noexcept { return ara_dc_id_; }

    // Host-ref maps, keyed by the uint64_t handle we send over IPC.
    // These let task 8 (host callbacks) resolve handles back to host refs.
    // Protected by host_refs_mutex_; acquire before reading or writing.
    std::mutex host_refs_mutex_;
    std::unordered_map<uint64_t, ARA::ARAMusicalContextHostRef>
        musical_context_host_refs_;
    std::unordered_map<uint64_t, ARA::ARARegionSequenceHostRef>
        region_sequence_host_refs_;
    std::unordered_map<uint64_t, ARA::ARAAudioSourceHostRef>
        audio_source_host_refs_;
    // Channel count for each audio source, keyed by the same handle as
    // audio_source_host_refs_. Used to fill CreateAudioReader responses.
    std::unordered_map<uint64_t, int32_t>
        audio_source_channel_counts_;
    std::unordered_map<uint64_t, ARA::ARAAudioModificationHostRef>
        audio_modification_host_refs_;
    std::unordered_map<uint64_t, ARA::ARAPlaybackRegionHostRef>
        playback_region_host_refs_;

    // Content reader host refs, created by the host and tracked per DC.
    std::unordered_map<uint64_t, ARA::ARAContentReaderHostRef>
        content_reader_host_refs_;
    std::atomic_uint64_t next_content_reader_handle_{1};

    // DC-side content reader type tracking (plugin calls our DC to read its
    // own analysis results; we need the type to deserialize event data).
    std::unordered_map<uint64_t, ARA::ARAContentType>
        content_reader_type_map_;

    // Per-reader event data cache, keyed by content reader handle.
    // Protected by host_refs_mutex_.
    struct ReaderEventCache {
        std::vector<uint8_t> last_event_data;
        union DecodedEvent {
            ARA::ARAContentNote          note;
            ARA::ARAContentTempoEntry    tempo;
            ARA::ARAContentBarSignature  bar;
            ARA::ARAContentTuning        tuning;
            ARA::ARAContentKeySignature  key;
            ARA::ARAContentChord         chord;
            DecodedEvent() noexcept { std::memset(this, 0, sizeof(*this)); }
        } decoded_event;
        std::string decoded_chord_name;
    };
    std::unordered_map<uint64_t, ReaderEventCache> content_reader_caches_;

    // Audio reader host refs created by the host.
    std::unordered_map<uint64_t, ARA::ARAAudioReaderHostRef>
        audio_reader_host_refs_;
    std::atomic_uint64_t next_audio_reader_handle_{1};

    // Per-reader shared memory buffers for YaAra::ReadAudioSamples.
    // Protected by audio_reader_shm_mutex_ (separate from host_refs_mutex_
    // to allow concurrent ReadAudioSamples and Create/Destroy calls).
    std::mutex audio_reader_shm_mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<AudioShmBuffer>> audio_reader_shm_buffers_;
    // Max samples per channel for each reader, keyed by reader ID.
    // Protected by audio_reader_shm_mutex_.
    std::unordered_map<uint64_t, uint32_t> audio_reader_max_samples_;

    // The DAW's host callback interfaces, valid for the lifetime of this DC.
    const ARA::ARADocumentControllerHostInstance* host_instance_ = nullptr;

    // Pointer to the factory C struct that owns this DC. Set by
    // register_ara_document_controller so get_factory() can return it.
    const ARA::ARAFactory* factory_ = nullptr;

   private:
    static AraDocumentControllerProxy* self(
        ARA::ARADocumentControllerRef ref) noexcept {
        return reinterpret_cast<AraDocumentControllerProxy*>(ref);
    }

    // Trampoline implementations
    static void ARA_CALL destroy(ARA::ARADocumentControllerRef r);
    static const ARA::ARAFactory* ARA_CALL
    get_factory(ARA::ARADocumentControllerRef r);
    static void ARA_CALL begin_editing(ARA::ARADocumentControllerRef r);
    static void ARA_CALL end_editing(ARA::ARADocumentControllerRef r);
    static void ARA_CALL
    notify_model_updates(ARA::ARADocumentControllerRef r);
    static ARA::ARABool ARA_CALL begin_restoring_document_from_archive(
        ARA::ARADocumentControllerRef r,
        ARA::ARAArchiveReaderHostRef archiveReaderHostRef);
    static ARA::ARABool ARA_CALL end_restoring_document_from_archive(
        ARA::ARADocumentControllerRef r,
        ARA::ARAArchiveReaderHostRef archiveReaderHostRef);
    static ARA::ARABool ARA_CALL
    store_document_to_archive(ARA::ARADocumentControllerRef r,
                              ARA::ARAArchiveWriterHostRef archiveWriterHostRef);
    static void ARA_CALL
    update_document_properties(ARA::ARADocumentControllerRef r,
                               const ARA::ARADocumentProperties* properties);
    static ARA::ARAMusicalContextRef ARA_CALL create_musical_context(
        ARA::ARADocumentControllerRef r,
        ARA::ARAMusicalContextHostRef hostRef,
        const ARA::ARAMusicalContextProperties* properties);
    static void ARA_CALL update_musical_context_properties(
        ARA::ARADocumentControllerRef r,
        ARA::ARAMusicalContextRef musicalContextRef,
        const ARA::ARAMusicalContextProperties* properties);
    static void ARA_CALL update_musical_context_content(
        ARA::ARADocumentControllerRef r,
        ARA::ARAMusicalContextRef musicalContextRef,
        const ARA::ARAContentTimeRange* range,
        ARA::ARAContentUpdateFlags flags);
    static void ARA_CALL
    destroy_musical_context(ARA::ARADocumentControllerRef r,
                            ARA::ARAMusicalContextRef musicalContextRef);
    static ARA::ARAAudioSourceRef ARA_CALL
    create_audio_source(ARA::ARADocumentControllerRef r,
                        ARA::ARAAudioSourceHostRef hostRef,
                        const ARA::ARAAudioSourceProperties* properties);
    static void ARA_CALL update_audio_source_properties(
        ARA::ARADocumentControllerRef r,
        ARA::ARAAudioSourceRef audioSourceRef,
        const ARA::ARAAudioSourceProperties* properties);
    static void ARA_CALL
    update_audio_source_content(ARA::ARADocumentControllerRef r,
                                ARA::ARAAudioSourceRef audioSourceRef,
                                const ARA::ARAContentTimeRange* range,
                                ARA::ARAContentUpdateFlags flags);
    static void ARA_CALL
    enable_audio_source_samples_access(ARA::ARADocumentControllerRef r,
                                       ARA::ARAAudioSourceRef audioSourceRef,
                                       ARA::ARABool enable);
    static void ARA_CALL deactivate_audio_source_for_undo_history(
        ARA::ARADocumentControllerRef r,
        ARA::ARAAudioSourceRef audioSourceRef,
        ARA::ARABool deactivate);
    static void ARA_CALL
    destroy_audio_source(ARA::ARADocumentControllerRef r,
                         ARA::ARAAudioSourceRef audioSourceRef);
    static ARA::ARAAudioModificationRef ARA_CALL create_audio_modification(
        ARA::ARADocumentControllerRef r,
        ARA::ARAAudioSourceRef audioSourceRef,
        ARA::ARAAudioModificationHostRef hostRef,
        const ARA::ARAAudioModificationProperties* properties);
    static ARA::ARAAudioModificationRef ARA_CALL clone_audio_modification(
        ARA::ARADocumentControllerRef r,
        ARA::ARAAudioModificationRef audioModificationRef,
        ARA::ARAAudioModificationHostRef hostRef,
        const ARA::ARAAudioModificationProperties* properties);
    static void ARA_CALL update_audio_modification_properties(
        ARA::ARADocumentControllerRef r,
        ARA::ARAAudioModificationRef audioModificationRef,
        const ARA::ARAAudioModificationProperties* properties);
    static void ARA_CALL deactivate_audio_modification_for_undo_history(
        ARA::ARADocumentControllerRef r,
        ARA::ARAAudioModificationRef audioModificationRef,
        ARA::ARABool deactivate);
    static void ARA_CALL
    destroy_audio_modification(ARA::ARADocumentControllerRef r,
                               ARA::ARAAudioModificationRef audioModificationRef);
    static ARA::ARAPlaybackRegionRef ARA_CALL create_playback_region(
        ARA::ARADocumentControllerRef r,
        ARA::ARAAudioModificationRef audioModificationRef,
        ARA::ARAPlaybackRegionHostRef hostRef,
        const ARA::ARAPlaybackRegionProperties* properties);
    static void ARA_CALL update_playback_region_properties(
        ARA::ARADocumentControllerRef r,
        ARA::ARAPlaybackRegionRef playbackRegionRef,
        const ARA::ARAPlaybackRegionProperties* properties);
    static void ARA_CALL
    destroy_playback_region(ARA::ARADocumentControllerRef r,
                            ARA::ARAPlaybackRegionRef playbackRegionRef);
    static ARA::ARABool ARA_CALL
    is_audio_source_content_available(ARA::ARADocumentControllerRef r,
                                      ARA::ARAAudioSourceRef audioSourceRef,
                                      ARA::ARAContentType contentType);
    static ARA::ARABool ARA_CALL is_audio_source_content_analysis_incomplete(
        ARA::ARADocumentControllerRef r,
        ARA::ARAAudioSourceRef audioSourceRef,
        ARA::ARAContentType contentType);
    static void ARA_CALL request_audio_source_content_analysis(
        ARA::ARADocumentControllerRef r,
        ARA::ARAAudioSourceRef audioSourceRef,
        ARA::ARASize contentTypesCount,
        const ARA::ARAContentType contentTypes[]);
    static ARA::ARAContentGrade ARA_CALL
    get_audio_source_content_grade(ARA::ARADocumentControllerRef r,
                                   ARA::ARAAudioSourceRef audioSourceRef,
                                   ARA::ARAContentType contentType);
    static ARA::ARAContentReaderRef ARA_CALL create_audio_source_content_reader(
        ARA::ARADocumentControllerRef r,
        ARA::ARAAudioSourceRef audioSourceRef,
        ARA::ARAContentType contentType,
        const ARA::ARAContentTimeRange* range);
    static ARA::ARABool ARA_CALL is_audio_modification_content_available(
        ARA::ARADocumentControllerRef r,
        ARA::ARAAudioModificationRef audioModificationRef,
        ARA::ARAContentType contentType);
    static ARA::ARAContentGrade ARA_CALL get_audio_modification_content_grade(
        ARA::ARADocumentControllerRef r,
        ARA::ARAAudioModificationRef audioModificationRef,
        ARA::ARAContentType contentType);
    static ARA::ARAContentReaderRef ARA_CALL
    create_audio_modification_content_reader(
        ARA::ARADocumentControllerRef r,
        ARA::ARAAudioModificationRef audioModificationRef,
        ARA::ARAContentType contentType,
        const ARA::ARAContentTimeRange* range);
    static ARA::ARABool ARA_CALL
    is_playback_region_content_available(ARA::ARADocumentControllerRef r,
                                         ARA::ARAPlaybackRegionRef playbackRegionRef,
                                         ARA::ARAContentType contentType);
    static ARA::ARAContentGrade ARA_CALL get_playback_region_content_grade(
        ARA::ARADocumentControllerRef r,
        ARA::ARAPlaybackRegionRef playbackRegionRef,
        ARA::ARAContentType contentType);
    static ARA::ARAContentReaderRef ARA_CALL
    create_playback_region_content_reader(
        ARA::ARADocumentControllerRef r,
        ARA::ARAPlaybackRegionRef playbackRegionRef,
        ARA::ARAContentType contentType,
        const ARA::ARAContentTimeRange* range);
    static ARA::ARAInt32 ARA_CALL
    get_content_reader_event_count(ARA::ARADocumentControllerRef r,
                                   ARA::ARAContentReaderRef contentReaderRef);
    static const void* ARA_CALL
    get_content_reader_data_for_event(ARA::ARADocumentControllerRef r,
                                      ARA::ARAContentReaderRef contentReaderRef,
                                      ARA::ARAInt32 eventIndex);
    static void ARA_CALL
    destroy_content_reader(ARA::ARADocumentControllerRef r,
                           ARA::ARAContentReaderRef contentReaderRef);
    static ARA::ARARegionSequenceRef ARA_CALL create_region_sequence(
        ARA::ARADocumentControllerRef r,
        ARA::ARARegionSequenceHostRef hostRef,
        const ARA::ARARegionSequenceProperties* properties);
    static void ARA_CALL update_region_sequence_properties(
        ARA::ARADocumentControllerRef r,
        ARA::ARARegionSequenceRef regionSequenceRef,
        const ARA::ARARegionSequenceProperties* properties);
    static void ARA_CALL
    destroy_region_sequence(ARA::ARADocumentControllerRef r,
                            ARA::ARARegionSequenceRef regionSequenceRef);
    static void ARA_CALL get_playback_region_head_and_tail_time(
        ARA::ARADocumentControllerRef r,
        ARA::ARAPlaybackRegionRef playbackRegionRef,
        ARA::ARATimeDuration* headTime,
        ARA::ARATimeDuration* tailTime);
    static ARA::ARABool ARA_CALL restore_objects_from_archive(
        ARA::ARADocumentControllerRef r,
        ARA::ARAArchiveReaderHostRef archiveReaderHostRef,
        const ARA::ARARestoreObjectsFilter* filter);
    static ARA::ARABool ARA_CALL
    store_objects_to_archive(ARA::ARADocumentControllerRef r,
                             ARA::ARAArchiveWriterHostRef archiveWriterHostRef,
                             const ARA::ARAStoreObjectsFilter* filter);
    static ARA::ARAInt32 ARA_CALL
    get_processing_algorithms_count(ARA::ARADocumentControllerRef r);
    static const ARA::ARAProcessingAlgorithmProperties* ARA_CALL
    get_processing_algorithm_properties(ARA::ARADocumentControllerRef r,
                                        ARA::ARAInt32 algorithmIndex);
    static ARA::ARAInt32 ARA_CALL get_processing_algorithm_for_audio_source(
        ARA::ARADocumentControllerRef r,
        ARA::ARAAudioSourceRef audioSourceRef);
    static void ARA_CALL request_processing_algorithm_for_audio_source(
        ARA::ARADocumentControllerRef r,
        ARA::ARAAudioSourceRef audioSourceRef,
        ARA::ARAInt32 algorithmIndex);
    static ARA::ARABool ARA_CALL
    is_licensed_for_capabilities(ARA::ARADocumentControllerRef r,
                                 ARA::ARABool runModalActivationDialogIfNeeded,
                                 ARA::ARASize contentTypesCount,
                                 const ARA::ARAContentType contentTypes[],
                                 ARA::ARAPlaybackTransformationFlags transformationFlags);
    static ARA::ARABool ARA_CALL store_audio_source_to_audio_file_chunk(
        ARA::ARADocumentControllerRef r,
        ARA::ARAArchiveWriterHostRef archiveWriterHostRef,
        ARA::ARAAudioSourceRef audioSourceRef,
        ARA::ARAPersistentID* documentArchiveID,
        ARA::ARABool* openAutomatically);
    static ARA::ARABool ARA_CALL is_audio_modification_preserving_audio_source_signal(
        ARA::ARADocumentControllerRef r,
        ARA::ARAAudioModificationRef audioModificationRef);

    native_size_t ara_dc_id_;
    Vst3PluginBridge& bridge_;

    ARA::ARADocumentControllerInterface iface_{};
    ARA::ARADocumentControllerInstance ara_dc_instance_{};

    std::atomic_uint64_t next_musical_context_handle_{1};
    std::atomic_uint64_t next_region_sequence_handle_{1};
    std::atomic_uint64_t next_audio_source_handle_{1};
    std::atomic_uint64_t next_audio_modification_handle_{1};
    std::atomic_uint64_t next_playback_region_handle_{1};
};

#endif  // WITH_ARA
