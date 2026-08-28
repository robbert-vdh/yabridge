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

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../ara-wine-abi.h"
#include <ARAInterface.h>

#include "../../../common/audio-shm.h"
#include "../../../common/serialization/vst3/ara-document-controller.h"

class Vst3Bridge;


// Owns the five ARA host callback interface structs and their host refs.
// Must not be moved or copied after construction since the plugin holds
// pointers into the interface structs.
struct AraHostInstanceProxy {
    AraHostInstanceProxy(native_size_t ara_dc_id, Vst3Bridge& bridge) noexcept;

    AraHostInstanceProxy(const AraHostInstanceProxy&) = delete;
    AraHostInstanceProxy& operator=(const AraHostInstanceProxy&) = delete;
    AraHostInstanceProxy(AraHostInstanceProxy&&) = delete;
    AraHostInstanceProxy& operator=(AraHostInstanceProxy&&) = delete;

    ARA::ARADocumentControllerHostInstance build_host_instance() const noexcept;

    // Five host interface tables
    ARA::ARAAudioAccessControllerInterface   audio_access_iface{};
    ARA::ARAArchivingControllerInterface     archiving_iface{};
    ARA::ARAContentAccessControllerInterface content_access_iface{};
    ARA::ARAModelUpdateControllerInterface   model_update_iface{};
    ARA::ARAPlaybackControllerInterface      playback_iface{};

    // Host refs passed to the plugin (encode ara_dc_id for routing)
    ARA::ARAAudioAccessControllerHostRef   audio_access_host_ref{};
    ARA::ARAArchivingControllerHostRef     archiving_host_ref{};
    ARA::ARAContentAccessControllerHostRef content_access_host_ref{};
    ARA::ARAModelUpdateControllerHostRef   model_update_host_ref{};
    ARA::ARAPlaybackControllerHostRef      playback_host_ref{};

    Vst3Bridge& bridge_;
    native_size_t ara_dc_id_;

private:
    // Audio access trampolines
    static ARA::ARAAudioReaderHostRef ARA_CALL
    create_audio_reader(ARA::ARAAudioAccessControllerHostRef h,
                        ARA::ARAAudioSourceHostRef source,
                        ARA::ARABool use64bit);
    static ARA::ARABool ARA_CALL
    read_audio_samples(ARA::ARAAudioAccessControllerHostRef h,
                       ARA::ARAAudioReaderHostRef reader,
                       ARA::ARASamplePosition pos,
                       ARA::ARASampleCount count,
                       void* const buffers[]);
    static void ARA_CALL
    destroy_audio_reader(ARA::ARAAudioAccessControllerHostRef h,
                         ARA::ARAAudioReaderHostRef reader);

    // Archiving trampolines
    static ARA::ARASize ARA_CALL
    get_archive_size(ARA::ARAArchivingControllerHostRef h,
                     ARA::ARAArchiveReaderHostRef reader);
    static ARA::ARABool ARA_CALL
    read_bytes_from_archive(ARA::ARAArchivingControllerHostRef h,
                            ARA::ARAArchiveReaderHostRef reader,
                            ARA::ARASize position,
                            ARA::ARASize length,
                            ARA::ARAByte buffer[]);
    static ARA::ARABool ARA_CALL
    write_bytes_to_archive(ARA::ARAArchivingControllerHostRef h,
                           ARA::ARAArchiveWriterHostRef writer,
                           ARA::ARASize position,
                           ARA::ARASize length,
                           const ARA::ARAByte buffer[]);
    static void ARA_CALL
    notify_document_archiving_progress(ARA::ARAArchivingControllerHostRef h,
                                       float value);
    static void ARA_CALL
    notify_document_unarchiving_progress(ARA::ARAArchivingControllerHostRef h,
                                         float value);
    static ARA::ARAPersistentID ARA_CALL
    get_document_archive_id(ARA::ARAArchivingControllerHostRef h,
                            ARA::ARAArchiveReaderHostRef reader);

    // Content access trampolines
    static ARA::ARABool ARA_CALL
    is_musical_context_content_available(
        ARA::ARAContentAccessControllerHostRef h,
        ARA::ARAMusicalContextHostRef ctx,
        ARA::ARAContentType type);
    static ARA::ARAContentGrade ARA_CALL
    get_musical_context_content_grade(
        ARA::ARAContentAccessControllerHostRef h,
        ARA::ARAMusicalContextHostRef ctx,
        ARA::ARAContentType type);
    static ARA::ARAContentReaderHostRef ARA_CALL
    create_musical_context_content_reader(
        ARA::ARAContentAccessControllerHostRef h,
        ARA::ARAMusicalContextHostRef ctx,
        ARA::ARAContentType type,
        const ARA::ARAContentTimeRange* range);
    static ARA::ARABool ARA_CALL
    is_audio_source_content_available(
        ARA::ARAContentAccessControllerHostRef h,
        ARA::ARAAudioSourceHostRef source,
        ARA::ARAContentType type);
    static ARA::ARAContentGrade ARA_CALL
    get_audio_source_content_grade(
        ARA::ARAContentAccessControllerHostRef h,
        ARA::ARAAudioSourceHostRef source,
        ARA::ARAContentType type);
    static ARA::ARAContentReaderHostRef ARA_CALL
    create_audio_source_content_reader(
        ARA::ARAContentAccessControllerHostRef h,
        ARA::ARAAudioSourceHostRef source,
        ARA::ARAContentType type,
        const ARA::ARAContentTimeRange* range);
    static ARA::ARAInt32 ARA_CALL
    get_content_reader_event_count(ARA::ARAContentAccessControllerHostRef h,
                                   ARA::ARAContentReaderHostRef reader);
    static const void* ARA_CALL
    get_content_reader_data_for_event(ARA::ARAContentAccessControllerHostRef h,
                                      ARA::ARAContentReaderHostRef reader,
                                      ARA::ARAInt32 index);
    static void ARA_CALL
    destroy_content_reader(ARA::ARAContentAccessControllerHostRef h,
                           ARA::ARAContentReaderHostRef reader);

    // Model update trampolines (async fire-and-forget)
    static void ARA_CALL
    notify_audio_source_analysis_progress(
        ARA::ARAModelUpdateControllerHostRef h,
        ARA::ARAAudioSourceHostRef source,
        ARA::ARAAnalysisProgressState state,
        float value);
    static void ARA_CALL
    notify_audio_source_content_changed(
        ARA::ARAModelUpdateControllerHostRef h,
        ARA::ARAAudioSourceHostRef source,
        const ARA::ARAContentTimeRange* range,
        ARA::ARAContentUpdateFlags flags);
    static void ARA_CALL
    notify_audio_modification_content_changed(
        ARA::ARAModelUpdateControllerHostRef h,
        ARA::ARAAudioModificationHostRef mod,
        const ARA::ARAContentTimeRange* range,
        ARA::ARAContentUpdateFlags flags);
    static void ARA_CALL
    notify_playback_region_content_changed(
        ARA::ARAModelUpdateControllerHostRef h,
        ARA::ARAPlaybackRegionHostRef region,
        const ARA::ARAContentTimeRange* range,
        ARA::ARAContentUpdateFlags flags);
    static void ARA_CALL
    notify_document_data_changed(ARA::ARAModelUpdateControllerHostRef h);

    // Playback trampolines
    static void ARA_CALL
    request_start_playback(ARA::ARAPlaybackControllerHostRef h);
    static void ARA_CALL
    request_stop_playback(ARA::ARAPlaybackControllerHostRef h);
    static void ARA_CALL
    request_set_playback_position(ARA::ARAPlaybackControllerHostRef h,
                                  ARA::ARATimePosition pos);
    static void ARA_CALL
    request_set_cycle_range(ARA::ARAPlaybackControllerHostRef h,
                            ARA::ARATimePosition start,
                            ARA::ARATimeDuration duration);
    static void ARA_CALL
    request_enable_cycle(ARA::ARAPlaybackControllerHostRef h,
                         ARA::ARABool enable);

    // Cached archive ID string returned by get_document_archive_id()
    mutable std::mutex last_event_mutex_;
    mutable std::string last_archive_id_;

    // Cached content event data returned by get_content_reader_data_for_event()
    mutable std::vector<uint8_t> last_event_data_;

    // Storage for deserialized ARAContentChord (name pointer lifetime).
    mutable ARA::ARAContentChord last_chord_{};
    mutable ARA::ARAContentTuning last_tuning_{};
    mutable ARA::ARAContentKeySignature last_key_sig_{};
    mutable std::string last_chord_name_;

    // Maps content reader handle -> ARAContentType, populated by
    // create_{musical_context,audio_source}_content_reader and erased by
    // destroy_content_reader so get_content_reader_data_for_event can send the
    // correct type.
    std::mutex content_reader_type_map_mutex_;
    std::unordered_map<uint64_t, ARA::ARAContentType> content_reader_type_map_;

    // Maps audio reader handle -> channel count, protected by
    // audio_reader_map_mutex_ since readAudioSamples() may be called from
    // any thread concurrently with create/destroy.
    std::mutex audio_reader_map_mutex_;
    std::unordered_map<uint64_t, int32_t> audio_reader_channel_count_map_;
    std::unordered_map<uint64_t, uint32_t> audio_reader_frame_capacity_map_;

    // Per-reader shared memory buffers created by YaAra::CreateAudioReader.
    // Keyed on reader_id, same mutex as above.
    std::unordered_map<uint64_t, AudioShmBuffer> audio_reader_shm_buffers_;
    std::unordered_map<uint64_t, bool> audio_reader_use64_map_;
};

// Stores the plugin-side document controller ref and the host callback proxies.
struct AraDocumentControllerInstance {
    AraDocumentControllerInstance(ARA::ARADocumentControllerRef dc_ref,
                                  native_size_t ara_dc_id,
                                  Vst3Bridge& bridge) noexcept;

    AraDocumentControllerInstance(const AraDocumentControllerInstance&) = delete;
    AraDocumentControllerInstance& operator=(const AraDocumentControllerInstance&) = delete;

    // The full instance struct as returned by createDocumentControllerWithDocument.
    // Null until CreateDocumentController succeeds.
    const ARA::ARADocumentControllerInstance* dc_instance = nullptr;
    ARA::ARADocumentControllerRef dc_ref = nullptr;
    AraHostInstanceProxy host_proxy;

    // Built from host_proxy and passed to createDocumentControllerWithDocument.
    // Stored here so the pointer remains valid for the lifetime of the DC.
    ARA::ARADocumentControllerHostInstance host_instance{};

    // Tracks the number of audio sources currently in the document graph.
    // Needed to skip endRestoringDocumentFromArchive when none exist.
    int32_t audio_source_count = 0;
};

#endif  // WITH_ARA
