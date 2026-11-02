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

#include "ara-document-controller-proxy.h"

#include "../vst3.h"

namespace {

YaAraDocumentProperties to_ya(const ARA::ARADocumentProperties* p) {
    YaAraDocumentProperties out{};
    if (p && ARA_IMPLEMENTS_FIELD(p, ARADocumentProperties, name) && p->name)
        out.name = p->name;
    return out;
}

YaAraMusicalContextProperties to_ya(
    const ARA::ARAMusicalContextProperties* p) {
    YaAraMusicalContextProperties out{};
    if (!p)
        return out;
    if (ARA_IMPLEMENTS_FIELD(p, ARAMusicalContextProperties, name) && p->name)
        out.name = p->name;
    if (ARA_IMPLEMENTS_FIELD(p, ARAMusicalContextProperties, orderIndex))
        out.order_index = p->orderIndex;
    if (ARA_IMPLEMENTS_FIELD(p, ARAMusicalContextProperties, color) && p->color)
        out.color = YaAraColor{p->color->r, p->color->g, p->color->b};
    return out;
}


YaAraRegionSequenceProperties to_ya(
    const ARA::ARARegionSequenceProperties* p) {
    YaAraRegionSequenceProperties out{};
    if (!p)
        return out;
    if (p->name)
        out.name = p->name;
    out.order_index = p->orderIndex;
    out.musical_context_ref =
        reinterpret_cast<uint64_t>(p->musicalContextRef);
    if (ARA_IMPLEMENTS_FIELD(p, ARARegionSequenceProperties, color) && p->color)
        out.color = YaAraColor{p->color->r, p->color->g, p->color->b};
    return out;
}

YaAraAudioSourceProperties to_ya(const ARA::ARAAudioSourceProperties* p) {
    YaAraAudioSourceProperties out{};
    if (!p)
        return out;
    if (p->name)
        out.name = p->name;
    out.persistent_id = p->persistentID ? p->persistentID : "";
    out.sample_count = p->sampleCount;
    out.sample_rate = p->sampleRate;
    out.channel_count = p->channelCount;
    out.merits_64bit_samples = p->merits64BitSamples;
    if (ARA_IMPLEMENTS_FIELD(p, ARAAudioSourceProperties,
                             channelArrangementDataType) &&
        p->channelArrangementDataType ==
            ARA::kARAChannelArrangementVST3SpeakerArrangement &&
        p->channelArrangement) {
        // Encode channel arrangement data as raw bytes
        // The data size depends on the companion API type; we store it as bytes
        // and the Wine side decodes it using the companion API context.
        // For VST3, channelArrangement is a Steinberg::Vst::SpeakerArrangement*
        // (uint64_t). We store 8 bytes.
        YaAraSampleChannelArrangement arr{};
        arr.data_type =
            static_cast<int32_t>(p->channelArrangementDataType);
        const uint8_t* bytes =
            static_cast<const uint8_t*>(p->channelArrangement);
        // We don't know the exact size without the companion API context, so
        // for VST3 (kARAChannelArrangementVST3SpeakerArrangement) it is 8 bytes
        constexpr size_t vst3_arr_size =
            sizeof(Steinberg::Vst::SpeakerArrangement);
        // VST3 SpeakerArrangement is uint64_t (8 bytes) per specification
        arr.data.assign(bytes, bytes + vst3_arr_size);
        out.channel_arrangement = std::move(arr);
    }
    return out;
}


YaAraAudioModificationProperties to_ya(
    const ARA::ARAAudioModificationProperties* p) {
    YaAraAudioModificationProperties out{};
    if (!p)
        return out;
    if (p->name)
        out.name = p->name;
    out.persistent_id = p->persistentID ? p->persistentID : "";
    return out;
}

YaAraPlaybackRegionProperties to_ya(
    const ARA::ARAPlaybackRegionProperties* p) {
    YaAraPlaybackRegionProperties out{};
    if (!p)
        return out;
    out.transformation_flags =
        static_cast<int32_t>(p->transformationFlags);
    out.start_in_modification_time = p->startInModificationTime;
    out.duration_in_modification_time = p->durationInModificationTime;
    out.start_in_playback_time = p->startInPlaybackTime;
    out.duration_in_playback_time = p->durationInPlaybackTime;
    out.musical_context_ref =
        reinterpret_cast<uint64_t>(p->musicalContextRef);
    if (ARA_IMPLEMENTS_FIELD(p, ARAPlaybackRegionProperties,
                             regionSequenceRef))
        out.region_sequence_ref =
            reinterpret_cast<uint64_t>(p->regionSequenceRef);
    if (ARA_IMPLEMENTS_FIELD(p, ARAPlaybackRegionProperties, name) && p->name)
        out.name = p->name;
    if (ARA_IMPLEMENTS_FIELD(p, ARAPlaybackRegionProperties, color) && p->color)
        out.color = YaAraColor{p->color->r, p->color->g, p->color->b};
    return out;
}

std::optional<YaAraContentTimeRange> to_ya(
    const ARA::ARAContentTimeRange* r) {
    if (!r)
        return std::nullopt;
    return YaAraContentTimeRange{r->start, r->duration};
}

}  // namespace


AraDocumentControllerProxy::AraDocumentControllerProxy(
    Vst3PluginBridge& bridge,
    native_size_t ara_dc_id) noexcept
    : ara_dc_id_(ara_dc_id), bridge_(bridge) {
    iface_.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
        ARADocumentControllerInterface,
        isAudioModificationPreservingAudioSourceSignal);
    iface_.destroyDocumentController = destroy;
    iface_.getFactory = get_factory;
    iface_.beginEditing = begin_editing;
    iface_.endEditing = end_editing;
    iface_.notifyModelUpdates = notify_model_updates;
    iface_.beginRestoringDocumentFromArchive =
        begin_restoring_document_from_archive;
    iface_.endRestoringDocumentFromArchive =
        end_restoring_document_from_archive;
    iface_.storeDocumentToArchive = store_document_to_archive;
    iface_.updateDocumentProperties = update_document_properties;
    iface_.createMusicalContext = create_musical_context;
    iface_.updateMusicalContextProperties = update_musical_context_properties;
    iface_.updateMusicalContextContent = update_musical_context_content;
    iface_.destroyMusicalContext = destroy_musical_context;
    iface_.createAudioSource = create_audio_source;
    iface_.updateAudioSourceProperties = update_audio_source_properties;
    iface_.updateAudioSourceContent = update_audio_source_content;
    iface_.enableAudioSourceSamplesAccess = enable_audio_source_samples_access;
    iface_.deactivateAudioSourceForUndoHistory =
        deactivate_audio_source_for_undo_history;
    iface_.destroyAudioSource = destroy_audio_source;
    iface_.createAudioModification = create_audio_modification;
    iface_.cloneAudioModification = clone_audio_modification;
    iface_.updateAudioModificationProperties =
        update_audio_modification_properties;
    iface_.deactivateAudioModificationForUndoHistory =
        deactivate_audio_modification_for_undo_history;
    iface_.destroyAudioModification = destroy_audio_modification;
    iface_.createPlaybackRegion = create_playback_region;
    iface_.updatePlaybackRegionProperties = update_playback_region_properties;
    iface_.destroyPlaybackRegion = destroy_playback_region;
    iface_.isAudioSourceContentAvailable = is_audio_source_content_available;
    iface_.isAudioSourceContentAnalysisIncomplete =
        is_audio_source_content_analysis_incomplete;
    iface_.requestAudioSourceContentAnalysis =
        request_audio_source_content_analysis;
    iface_.getAudioSourceContentGrade = get_audio_source_content_grade;
    iface_.createAudioSourceContentReader = create_audio_source_content_reader;
    iface_.isAudioModificationContentAvailable =
        is_audio_modification_content_available;
    iface_.getAudioModificationContentGrade =
        get_audio_modification_content_grade;
    iface_.createAudioModificationContentReader =
        create_audio_modification_content_reader;
    iface_.isPlaybackRegionContentAvailable =
        is_playback_region_content_available;
    iface_.getPlaybackRegionContentGrade = get_playback_region_content_grade;
    iface_.createPlaybackRegionContentReader =
        create_playback_region_content_reader;
    iface_.getContentReaderEventCount = get_content_reader_event_count;
    iface_.getContentReaderDataForEvent = get_content_reader_data_for_event;
    iface_.destroyContentReader = destroy_content_reader;
    iface_.createRegionSequence = create_region_sequence;
    iface_.updateRegionSequenceProperties = update_region_sequence_properties;
    iface_.destroyRegionSequence = destroy_region_sequence;
    iface_.getPlaybackRegionHeadAndTailTime =
        get_playback_region_head_and_tail_time;
    iface_.restoreObjectsFromArchive = restore_objects_from_archive;
    iface_.storeObjectsToArchive = store_objects_to_archive;
    iface_.getProcessingAlgorithmsCount = get_processing_algorithms_count;
    iface_.getProcessingAlgorithmProperties =
        get_processing_algorithm_properties;
    iface_.getProcessingAlgorithmForAudioSource =
        get_processing_algorithm_for_audio_source;
    iface_.requestProcessingAlgorithmForAudioSource =
        request_processing_algorithm_for_audio_source;
    iface_.isLicensedForCapabilities = is_licensed_for_capabilities;
    iface_.storeAudioSourceToAudioFileChunk =
        store_audio_source_to_audio_file_chunk;
    iface_.isAudioModificationPreservingAudioSourceSignal =
        is_audio_modification_preserving_audio_source_signal;

    ara_dc_instance_.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
        ARADocumentControllerInstance, documentControllerInterface);
    ara_dc_instance_.documentControllerRef =
        reinterpret_cast<ARA::ARADocumentControllerRef>(this);
    ara_dc_instance_.documentControllerInterface = &iface_;
}


// ---------------------------------------------------------------------------
// Trampolines
// ---------------------------------------------------------------------------

void ARA_CALL AraDocumentControllerProxy::destroy(
    ARA::ARADocumentControllerRef r) {
    auto* p = self(r);
    p->bridge_.send_message(
        YaAra::DestroyDocumentController{p->ara_dc_id_});
    // unregister_ara_document_controller drops the shared_ptr that owns *p,
    // so p is dangling after this call. Nothing may access p after this line.
    p->bridge_.unregister_ara_document_controller(p->ara_dc_id_);
}

const ARA::ARAFactory* ARA_CALL AraDocumentControllerProxy::get_factory(
    ARA::ARADocumentControllerRef r) {
    return self(r)->factory_;
}

void ARA_CALL AraDocumentControllerProxy::begin_editing(
    ARA::ARADocumentControllerRef r) {
    auto* p = self(r);
    p->bridge_.send_message(YaAra::BeginEditing{p->ara_dc_id_});
}

void ARA_CALL AraDocumentControllerProxy::end_editing(
    ARA::ARADocumentControllerRef r) {
    auto* p = self(r);
    p->bridge_.send_message(YaAra::EndEditing{p->ara_dc_id_});
}

void ARA_CALL AraDocumentControllerProxy::notify_model_updates(
    ARA::ARADocumentControllerRef r) {
    auto* p = self(r);
    p->bridge_.send_message(YaAra::NotifyModelUpdates{p->ara_dc_id_});
}

ARA::ARABool ARA_CALL
AraDocumentControllerProxy::begin_restoring_document_from_archive(
    ARA::ARADocumentControllerRef r,
    ARA::ARAArchiveReaderHostRef archiveReaderHostRef) {
    auto* p = self(r);
    auto result = p->bridge_.send_message(
        YaAra::BeginRestoringDocumentFromArchive{
            p->ara_dc_id_,
            reinterpret_cast<uint64_t>(archiveReaderHostRef)});
    return std::visit(
        [](auto&& v) -> ARA::ARABool {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>, int32_t>)
                return static_cast<ARA::ARABool>(v);
            else
                return ARA::kARAFalse;
        },
        result);
}

ARA::ARABool ARA_CALL
AraDocumentControllerProxy::end_restoring_document_from_archive(
    ARA::ARADocumentControllerRef r,
    ARA::ARAArchiveReaderHostRef archiveReaderHostRef) {
    auto* p = self(r);
    auto result = p->bridge_.send_message(
        YaAra::EndRestoringDocumentFromArchive{
            p->ara_dc_id_,
            reinterpret_cast<uint64_t>(archiveReaderHostRef)});
    return std::visit(
        [](auto&& v) -> ARA::ARABool {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>, int32_t>)
                return static_cast<ARA::ARABool>(v);
            else
                return ARA::kARAFalse;
        },
        result);
}

ARA::ARABool ARA_CALL AraDocumentControllerProxy::store_document_to_archive(
    ARA::ARADocumentControllerRef r,
    ARA::ARAArchiveWriterHostRef archiveWriterHostRef) {
    auto* p = self(r);
    auto result = p->bridge_.send_message(YaAra::StoreDocumentToArchive{
        p->ara_dc_id_,
        reinterpret_cast<uint64_t>(archiveWriterHostRef)});
    return std::visit(
        [](auto&& v) -> ARA::ARABool {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>, int32_t>)
                return static_cast<ARA::ARABool>(v);
            else
                return ARA::kARAFalse;
        },
        result);
}


void ARA_CALL AraDocumentControllerProxy::update_document_properties(
    ARA::ARADocumentControllerRef r,
    const ARA::ARADocumentProperties* properties) {
    auto* p = self(r);
    p->bridge_.send_message(YaAra::UpdateDocumentProperties{
        p->ara_dc_id_, to_ya(properties)});
}

ARA::ARAMusicalContextRef ARA_CALL
AraDocumentControllerProxy::create_musical_context(
    ARA::ARADocumentControllerRef r,
    ARA::ARAMusicalContextHostRef hostRef,
    const ARA::ARAMusicalContextProperties* properties) {
    auto* p = self(r);
    uint64_t handle = p->next_musical_context_handle_.fetch_add(1);
    {
        std::lock_guard lock(p->host_refs_mutex_);
        p->musical_context_host_refs_[handle] = hostRef;
    }
    auto result = p->bridge_.send_message(YaAra::AddMusicalContext{
        p->ara_dc_id_, handle, to_ya(properties)});
    return std::visit(
        [](auto&& v) -> ARA::ARAMusicalContextRef {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>, uint64_t>)
                return reinterpret_cast<ARA::ARAMusicalContextRef>(v);
            else
                return nullptr;
        },
        result);
}

void ARA_CALL AraDocumentControllerProxy::update_musical_context_properties(
    ARA::ARADocumentControllerRef r,
    ARA::ARAMusicalContextRef musicalContextRef,
    const ARA::ARAMusicalContextProperties* properties) {
    auto* p = self(r);
    p->bridge_.send_message(YaAra::UpdateMusicalContextProperties{
        p->ara_dc_id_,
        reinterpret_cast<uint64_t>(musicalContextRef),
        to_ya(properties)});
}

void ARA_CALL AraDocumentControllerProxy::update_musical_context_content(
    ARA::ARADocumentControllerRef r,
    ARA::ARAMusicalContextRef musicalContextRef,
    const ARA::ARAContentTimeRange* range,
    ARA::ARAContentUpdateFlags flags) {
    auto* p = self(r);
    p->bridge_.send_message(YaAra::UpdateMusicalContextContent{
        p->ara_dc_id_,
        reinterpret_cast<uint64_t>(musicalContextRef),
        to_ya(range),
        static_cast<int32_t>(flags)});
}

void ARA_CALL AraDocumentControllerProxy::destroy_musical_context(
    ARA::ARADocumentControllerRef r,
    ARA::ARAMusicalContextRef musicalContextRef) {
    auto* p = self(r);
    p->bridge_.send_message(YaAra::RemoveMusicalContext{
        p->ara_dc_id_,
        reinterpret_cast<uint64_t>(musicalContextRef)});
    {
        std::lock_guard lock(p->host_refs_mutex_);
        p->musical_context_host_refs_.erase(
            reinterpret_cast<uint64_t>(musicalContextRef));
    }
}


ARA::ARAAudioSourceRef ARA_CALL AraDocumentControllerProxy::create_audio_source(
    ARA::ARADocumentControllerRef r,
    ARA::ARAAudioSourceHostRef hostRef,
    const ARA::ARAAudioSourceProperties* properties) {
    auto* p = self(r);
    uint64_t handle = p->next_audio_source_handle_.fetch_add(1);
    const int32_t channel_count = properties ? properties->channelCount : 0;
    auto result = p->bridge_.send_message(YaAra::AddAudioSource{
        p->ara_dc_id_, handle, to_ya(properties)});
    return std::visit(
        [&](auto&& v) -> ARA::ARAAudioSourceRef {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>, uint64_t>) {
                std::lock_guard lock(p->host_refs_mutex_);
                p->audio_source_host_refs_[handle] = hostRef;
                p->audio_source_channel_counts_[handle] = channel_count;
                return reinterpret_cast<ARA::ARAAudioSourceRef>(v);
            } else {
                return nullptr;
            }
        },
        result);
}

void ARA_CALL AraDocumentControllerProxy::update_audio_source_properties(
    ARA::ARADocumentControllerRef r,
    ARA::ARAAudioSourceRef audioSourceRef,
    const ARA::ARAAudioSourceProperties* properties) {
    auto* p = self(r);
    p->bridge_.send_message(YaAra::UpdateAudioSourceProperties{
        p->ara_dc_id_,
        reinterpret_cast<uint64_t>(audioSourceRef),
        to_ya(properties)});
}

void ARA_CALL AraDocumentControllerProxy::update_audio_source_content(
    ARA::ARADocumentControllerRef r,
    ARA::ARAAudioSourceRef audioSourceRef,
    const ARA::ARAContentTimeRange* range,
    ARA::ARAContentUpdateFlags flags) {
    auto* p = self(r);
    p->bridge_.send_message(YaAra::UpdateAudioSourceContent{
        p->ara_dc_id_,
        reinterpret_cast<uint64_t>(audioSourceRef),
        to_ya(range),
        static_cast<int32_t>(flags)});
}

void ARA_CALL AraDocumentControllerProxy::enable_audio_source_samples_access(
    ARA::ARADocumentControllerRef r,
    ARA::ARAAudioSourceRef audioSourceRef,
    ARA::ARABool enable) {
    auto* p = self(r);
    p->bridge_.send_message(YaAra::EnableAudioSourceSamplesAccess{
        p->ara_dc_id_,
        reinterpret_cast<uint64_t>(audioSourceRef),
        static_cast<int32_t>(enable)});
}

void ARA_CALL
AraDocumentControllerProxy::deactivate_audio_source_for_undo_history(
    ARA::ARADocumentControllerRef r,
    ARA::ARAAudioSourceRef audioSourceRef,
    ARA::ARABool deactivate) {
    auto* p = self(r);
    p->bridge_.send_message(YaAra::DeactivateAndUnregisterAudioSource{
        p->ara_dc_id_,
        reinterpret_cast<uint64_t>(audioSourceRef),
        static_cast<int32_t>(deactivate)});
}

void ARA_CALL AraDocumentControllerProxy::destroy_audio_source(
    ARA::ARADocumentControllerRef r,
    ARA::ARAAudioSourceRef audioSourceRef) {
    auto* p = self(r);
    const uint64_t handle = reinterpret_cast<uint64_t>(audioSourceRef);
    p->bridge_.send_message(YaAra::RemoveAudioSource{p->ara_dc_id_, handle});
    {
        std::lock_guard lock(p->host_refs_mutex_);
        p->audio_source_host_refs_.erase(handle);
        p->audio_source_channel_counts_.erase(handle);
    }
}


ARA::ARAAudioModificationRef ARA_CALL
AraDocumentControllerProxy::create_audio_modification(
    ARA::ARADocumentControllerRef r,
    ARA::ARAAudioSourceRef audioSourceRef,
    ARA::ARAAudioModificationHostRef hostRef,
    const ARA::ARAAudioModificationProperties* properties) {
    auto* p = self(r);
    uint64_t handle = p->next_audio_modification_handle_.fetch_add(1);
    auto result = p->bridge_.send_message(YaAra::AddAudioModification{
        p->ara_dc_id_,
        reinterpret_cast<uint64_t>(audioSourceRef),
        handle,
        to_ya(properties)});
    return std::visit(
        [&](auto&& v) -> ARA::ARAAudioModificationRef {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>, uint64_t>) {
                std::lock_guard lock(p->host_refs_mutex_);
                p->audio_modification_host_refs_[handle] = hostRef;
                return reinterpret_cast<ARA::ARAAudioModificationRef>(v);
            } else {
                return nullptr;
            }
        },
        result);
}

ARA::ARAAudioModificationRef ARA_CALL
AraDocumentControllerProxy::clone_audio_modification(
    ARA::ARADocumentControllerRef r,
    ARA::ARAAudioModificationRef audioModificationRef,
    ARA::ARAAudioModificationHostRef hostRef,
    const ARA::ARAAudioModificationProperties* properties) {
    auto* p = self(r);
    uint64_t handle = p->next_audio_modification_handle_.fetch_add(1);
    auto result = p->bridge_.send_message(YaAra::CloneAudioModification{
        p->ara_dc_id_,
        reinterpret_cast<uint64_t>(audioModificationRef),
        handle,
        to_ya(properties)});
    return std::visit(
        [&](auto&& v) -> ARA::ARAAudioModificationRef {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>, uint64_t>) {
                std::lock_guard lock(p->host_refs_mutex_);
                p->audio_modification_host_refs_[handle] = hostRef;
                return reinterpret_cast<ARA::ARAAudioModificationRef>(v);
            } else {
                return nullptr;
            }
        },
        result);
}

void ARA_CALL AraDocumentControllerProxy::update_audio_modification_properties(
    ARA::ARADocumentControllerRef r,
    ARA::ARAAudioModificationRef audioModificationRef,
    const ARA::ARAAudioModificationProperties* properties) {
    auto* p = self(r);
    p->bridge_.send_message(YaAra::UpdateAudioModificationProperties{
        p->ara_dc_id_,
        reinterpret_cast<uint64_t>(audioModificationRef),
        to_ya(properties)});
}

void ARA_CALL
AraDocumentControllerProxy::deactivate_audio_modification_for_undo_history(
    ARA::ARADocumentControllerRef r,
    ARA::ARAAudioModificationRef audioModificationRef,
    ARA::ARABool deactivate) {
    auto* p = self(r);
    p->bridge_.send_message(YaAra::DeactivateAndUnregisterAudioModification{
        p->ara_dc_id_,
        reinterpret_cast<uint64_t>(audioModificationRef),
        static_cast<int32_t>(deactivate)});
}

void ARA_CALL AraDocumentControllerProxy::destroy_audio_modification(
    ARA::ARADocumentControllerRef r,
    ARA::ARAAudioModificationRef audioModificationRef) {
    auto* p = self(r);
    const uint64_t handle = reinterpret_cast<uint64_t>(audioModificationRef);
    p->bridge_.send_message(
        YaAra::RemoveAudioModification{p->ara_dc_id_, handle});
    {
        std::lock_guard lock(p->host_refs_mutex_);
        p->audio_modification_host_refs_.erase(handle);
    }
}


ARA::ARAPlaybackRegionRef ARA_CALL
AraDocumentControllerProxy::create_playback_region(
    ARA::ARADocumentControllerRef r,
    ARA::ARAAudioModificationRef audioModificationRef,
    ARA::ARAPlaybackRegionHostRef hostRef,
    const ARA::ARAPlaybackRegionProperties* properties) {
    auto* p = self(r);
    uint64_t handle = p->next_playback_region_handle_.fetch_add(1);
    auto result = p->bridge_.send_message(YaAra::AddPlaybackRegion{
        p->ara_dc_id_,
        reinterpret_cast<uint64_t>(audioModificationRef),
        handle,
        to_ya(properties)});
    return std::visit(
        [&](auto&& v) -> ARA::ARAPlaybackRegionRef {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>, uint64_t>) {
                std::lock_guard lock(p->host_refs_mutex_);
                p->playback_region_host_refs_[handle] = hostRef;
                return reinterpret_cast<ARA::ARAPlaybackRegionRef>(v);
            } else {
                return nullptr;
            }
        },
        result);
}

void ARA_CALL AraDocumentControllerProxy::update_playback_region_properties(
    ARA::ARADocumentControllerRef r,
    ARA::ARAPlaybackRegionRef playbackRegionRef,
    const ARA::ARAPlaybackRegionProperties* properties) {
    auto* p = self(r);
    p->bridge_.send_message(YaAra::UpdatePlaybackRegionProperties{
        p->ara_dc_id_,
        reinterpret_cast<uint64_t>(playbackRegionRef),
        to_ya(properties)});
}

void ARA_CALL AraDocumentControllerProxy::destroy_playback_region(
    ARA::ARADocumentControllerRef r,
    ARA::ARAPlaybackRegionRef playbackRegionRef) {
    auto* p = self(r);
    const uint64_t handle = reinterpret_cast<uint64_t>(playbackRegionRef);
    p->bridge_.send_message(
        YaAra::RemovePlaybackRegion{p->ara_dc_id_, handle});
    {
        std::lock_guard lock(p->host_refs_mutex_);
        p->playback_region_host_refs_.erase(handle);
    }
}

ARA::ARARegionSequenceRef ARA_CALL
AraDocumentControllerProxy::create_region_sequence(
    ARA::ARADocumentControllerRef r,
    ARA::ARARegionSequenceHostRef hostRef,
    const ARA::ARARegionSequenceProperties* properties) {
    auto* p = self(r);
    uint64_t handle = p->next_region_sequence_handle_.fetch_add(1);
    auto result = p->bridge_.send_message(YaAra::AddRegionSequence{
        p->ara_dc_id_, handle, to_ya(properties)});
    return std::visit(
        [&](auto&& v) -> ARA::ARARegionSequenceRef {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>, uint64_t>) {
                std::lock_guard lock(p->host_refs_mutex_);
                p->region_sequence_host_refs_[handle] = hostRef;
                return reinterpret_cast<ARA::ARARegionSequenceRef>(v);
            } else {
                return nullptr;
            }
        },
        result);
}

void ARA_CALL AraDocumentControllerProxy::update_region_sequence_properties(
    ARA::ARADocumentControllerRef r,
    ARA::ARARegionSequenceRef regionSequenceRef,
    const ARA::ARARegionSequenceProperties* properties) {
    auto* p = self(r);
    p->bridge_.send_message(YaAra::UpdateRegionSequenceProperties{
        p->ara_dc_id_,
        reinterpret_cast<uint64_t>(regionSequenceRef),
        to_ya(properties)});
}

void ARA_CALL AraDocumentControllerProxy::destroy_region_sequence(
    ARA::ARADocumentControllerRef r,
    ARA::ARARegionSequenceRef regionSequenceRef) {
    auto* p = self(r);
    const uint64_t handle = reinterpret_cast<uint64_t>(regionSequenceRef);
    p->bridge_.send_message(
        YaAra::RemoveRegionSequence{p->ara_dc_id_, handle});
    {
        std::lock_guard lock(p->host_refs_mutex_);
        p->region_sequence_host_refs_.erase(handle);
    }
}


ARA::ARABool ARA_CALL
AraDocumentControllerProxy::is_audio_source_content_available(
    ARA::ARADocumentControllerRef r,
    ARA::ARAAudioSourceRef audioSourceRef,
    ARA::ARAContentType contentType) {
    auto* p = self(r);
    return static_cast<ARA::ARABool>(
        p->bridge_.send_mutually_recursive_message(
            YaAra::IsAudioSourceContentAvailableDC{
                p->ara_dc_id_,
                reinterpret_cast<uint64_t>(audioSourceRef),
                static_cast<int32_t>(contentType)})
            .value);
}

ARA::ARABool ARA_CALL
AraDocumentControllerProxy::is_audio_source_content_analysis_incomplete(
    ARA::ARADocumentControllerRef /*r*/,
    ARA::ARAAudioSourceRef /*audioSourceRef*/,
    ARA::ARAContentType /*contentType*/) {
    return ARA::kARAFalse;
}

void ARA_CALL AraDocumentControllerProxy::request_audio_source_content_analysis(
    ARA::ARADocumentControllerRef r,
    ARA::ARAAudioSourceRef audioSourceRef,
    ARA::ARASize contentTypesCount,
    const ARA::ARAContentType contentTypes[]) {
    auto* p = self(r);
    std::vector<int32_t> types;
    types.reserve(contentTypesCount);
    for (ARA::ARASize i = 0; i < contentTypesCount; ++i)
        types.push_back(static_cast<int32_t>(contentTypes[i]));
    p->bridge_.send_message(YaAra::RequestAudioSourceContentAnalysis{
        p->ara_dc_id_,
        reinterpret_cast<uint64_t>(audioSourceRef),
        std::move(types)});
}

ARA::ARAContentGrade ARA_CALL
AraDocumentControllerProxy::get_audio_source_content_grade(
    ARA::ARADocumentControllerRef r,
    ARA::ARAAudioSourceRef audioSourceRef,
    ARA::ARAContentType contentType) {
    auto* p = self(r);
    return static_cast<ARA::ARAContentGrade>(
        p->bridge_.send_mutually_recursive_message(
            YaAra::GetAudioSourceContentGradeDC{
                p->ara_dc_id_,
                reinterpret_cast<uint64_t>(audioSourceRef),
                static_cast<int32_t>(contentType)})
            .value);
}

ARA::ARAContentReaderRef ARA_CALL
AraDocumentControllerProxy::create_audio_source_content_reader(
    ARA::ARADocumentControllerRef r,
    ARA::ARAAudioSourceRef audioSourceRef,
    ARA::ARAContentType contentType,
    const ARA::ARAContentTimeRange* range) {
    auto* p = self(r);
    std::optional<YaAraContentTimeRange> range_opt;
    if (range)
        range_opt = YaAraContentTimeRange{range->start, range->duration};
    const int32_t handle =
        p->bridge_.send_mutually_recursive_message(
            YaAra::CreateAudioSourceContentReaderDC{
                p->ara_dc_id_,
                reinterpret_cast<uint64_t>(audioSourceRef),
                static_cast<int32_t>(contentType),
                range_opt})
            .value;
    if (!handle)
        return nullptr;
    {
        std::lock_guard lock(p->host_refs_mutex_);
        p->content_reader_type_map_[static_cast<uint64_t>(handle)] =
            contentType;
    }
    return reinterpret_cast<ARA::ARAContentReaderRef>(
        static_cast<uint64_t>(handle));
}

ARA::ARABool ARA_CALL
AraDocumentControllerProxy::is_audio_modification_content_available(
    ARA::ARADocumentControllerRef /*r*/,
    ARA::ARAAudioModificationRef /*audioModificationRef*/,
    ARA::ARAContentType /*contentType*/) {
    return ARA::kARAFalse;
}

ARA::ARAContentGrade ARA_CALL
AraDocumentControllerProxy::get_audio_modification_content_grade(
    ARA::ARADocumentControllerRef /*r*/,
    ARA::ARAAudioModificationRef /*audioModificationRef*/,
    ARA::ARAContentType /*contentType*/) {
    return ARA::kARAContentGradeInitial;
}

ARA::ARAContentReaderRef ARA_CALL
AraDocumentControllerProxy::create_audio_modification_content_reader(
    ARA::ARADocumentControllerRef /*r*/,
    ARA::ARAAudioModificationRef /*audioModificationRef*/,
    ARA::ARAContentType /*contentType*/,
    const ARA::ARAContentTimeRange* /*range*/) {
    return nullptr;
}

ARA::ARABool ARA_CALL
AraDocumentControllerProxy::is_playback_region_content_available(
    ARA::ARADocumentControllerRef /*r*/,
    ARA::ARAPlaybackRegionRef /*playbackRegionRef*/,
    ARA::ARAContentType /*contentType*/) {
    return ARA::kARAFalse;
}

ARA::ARAContentGrade ARA_CALL
AraDocumentControllerProxy::get_playback_region_content_grade(
    ARA::ARADocumentControllerRef /*r*/,
    ARA::ARAPlaybackRegionRef /*playbackRegionRef*/,
    ARA::ARAContentType /*contentType*/) {
    return ARA::kARAContentGradeInitial;
}

ARA::ARAContentReaderRef ARA_CALL
AraDocumentControllerProxy::create_playback_region_content_reader(
    ARA::ARADocumentControllerRef /*r*/,
    ARA::ARAPlaybackRegionRef /*playbackRegionRef*/,
    ARA::ARAContentType /*contentType*/,
    const ARA::ARAContentTimeRange* /*range*/) {
    return nullptr;
}

ARA::ARAInt32 ARA_CALL AraDocumentControllerProxy::get_content_reader_event_count(
    ARA::ARADocumentControllerRef r,
    ARA::ARAContentReaderRef contentReaderRef) {
    auto* p = self(r);
    return static_cast<ARA::ARAInt32>(
        p->bridge_.send_mutually_recursive_message(
            YaAra::GetContentReaderEventCountDC{
                p->ara_dc_id_,
                reinterpret_cast<uint64_t>(contentReaderRef)})
            .value);
}

const void* ARA_CALL
AraDocumentControllerProxy::get_content_reader_data_for_event(
    ARA::ARADocumentControllerRef r,
    ARA::ARAContentReaderRef contentReaderRef,
    ARA::ARAInt32 eventIndex) {
    auto* p = self(r);
    const uint64_t handle = reinterpret_cast<uint64_t>(contentReaderRef);
    ARA::ARAContentType content_type = ARA::kARAContentTypeNotes;
    {
        std::lock_guard lock(p->host_refs_mutex_);
        auto it = p->content_reader_type_map_.find(handle);
        if (it != p->content_reader_type_map_.end())
            content_type = it->second;
    }
    const auto response =
        p->bridge_.send_mutually_recursive_message(
            YaAra::GetContentReaderDataForEventDC{
                p->ara_dc_id_,
                handle,
                static_cast<int32_t>(eventIndex),
                static_cast<int32_t>(content_type)});
    if (response.data.empty())
        return nullptr;

    std::lock_guard lock(p->host_refs_mutex_);
    auto& cache = p->content_reader_caches_[handle];
    cache.last_event_data = response.data;
    const auto& bytes = cache.last_event_data;

    switch (content_type) {
        case ARA::kARAContentTypeNotes:
            if (bytes.size() >= sizeof(ARA::ARAContentNote)) {
                cache.decoded_event.note = {};
                std::memcpy(&cache.decoded_event.note, bytes.data(),
                            sizeof(ARA::ARAContentNote));
                return &cache.decoded_event.note;
            }
            break;
        case ARA::kARAContentTypeTempoEntries:
            if (bytes.size() >= sizeof(ARA::ARAContentTempoEntry)) {
                cache.decoded_event.tempo = {};
                std::memcpy(&cache.decoded_event.tempo, bytes.data(),
                            sizeof(ARA::ARAContentTempoEntry));
                return &cache.decoded_event.tempo;
            }
            break;
        case ARA::kARAContentTypeBarSignatures:
            if (bytes.size() >= sizeof(ARA::ARAContentBarSignature)) {
                cache.decoded_event.bar = {};
                std::memcpy(&cache.decoded_event.bar, bytes.data(),
                            sizeof(ARA::ARAContentBarSignature));
                return &cache.decoded_event.bar;
            }
            break;
        case ARA::kARAContentTypeStaticTuning: {
            const uint8_t* src = bytes.data();
            const uint8_t* end = src + bytes.size();
            ARA::ARAContentTuning& t = cache.decoded_event.tuning;
            t = {};
            auto read = [&](void* dst, size_t n) -> bool {
                if (src + n > end) return false;
                std::memcpy(dst, src, n);
                src += n;
                return true;
            };
            if (!read(&t.concertPitchFrequency, sizeof(t.concertPitchFrequency)) ||
                !read(&t.tunings, sizeof(t.tunings)))
                break;
            uint32_t name_len = 0;
            if (!read(&name_len, sizeof(name_len)))
                break;
            if (src + name_len > end)
                break;
            cache.decoded_chord_name.assign(
                reinterpret_cast<const char*>(src), name_len);
            src += name_len;
            t.name = cache.decoded_chord_name.empty()
                         ? nullptr
                         : cache.decoded_chord_name.c_str();
            return &t;
        }
        case ARA::kARAContentTypeKeySignatures: {
            const uint8_t* src = bytes.data();
            const uint8_t* end = src + bytes.size();
            ARA::ARAContentKeySignature& k = cache.decoded_event.key;
            k = {};
            auto read = [&](void* dst, size_t n) -> bool {
                if (src + n > end) return false;
                std::memcpy(dst, src, n);
                src += n;
                return true;
            };
            if (!read(&k.root, sizeof(k.root)) ||
                !read(&k.intervals, sizeof(k.intervals)))
                break;
            uint32_t name_len = 0;
            if (!read(&name_len, sizeof(name_len)))
                break;
            if (src + name_len > end)
                break;
            cache.decoded_chord_name.assign(
                reinterpret_cast<const char*>(src), name_len);
            src += name_len;
            k.name = cache.decoded_chord_name.empty()
                         ? nullptr
                         : cache.decoded_chord_name.c_str();
            if (!read(&k.position, sizeof(k.position)))
                break;
            return &k;
        }
        case ARA::kARAContentTypeSheetChords:
            if (bytes.size() >= sizeof(ARA::ARAContentChord)) {
                cache.decoded_event.chord = {};
                std::memcpy(&cache.decoded_event.chord, bytes.data(),
                            sizeof(ARA::ARAContentChord));
                if (bytes.size() > sizeof(ARA::ARAContentChord)) {
                    cache.decoded_chord_name.assign(
                        reinterpret_cast<const char*>(
                            bytes.data() + sizeof(ARA::ARAContentChord)));
                    cache.decoded_event.chord.name =
                        cache.decoded_chord_name.c_str();
                } else {
                    cache.decoded_event.chord.name = nullptr;
                }
                return &cache.decoded_event.chord;
            }
            break;
        default:
            return bytes.data();
    }
    return nullptr;
}

void ARA_CALL AraDocumentControllerProxy::destroy_content_reader(
    ARA::ARADocumentControllerRef r,
    ARA::ARAContentReaderRef contentReaderRef) {
    auto* p = self(r);
    const uint64_t handle = reinterpret_cast<uint64_t>(contentReaderRef);
    {
        std::lock_guard lock(p->host_refs_mutex_);
        p->content_reader_type_map_.erase(handle);
        p->content_reader_caches_.erase(handle);
    }
    p->bridge_.send_message(YaAra::DestroyContentReaderDC{
        p->ara_dc_id_,
        handle});
}


void ARA_CALL AraDocumentControllerProxy::get_playback_region_head_and_tail_time(
    ARA::ARADocumentControllerRef r,
    ARA::ARAPlaybackRegionRef playbackRegionRef,
    ARA::ARATimeDuration* headTime,
    ARA::ARATimeDuration* tailTime) {
    auto* p = self(r);
    auto response =
        p->bridge_.send_message(YaAra::GetPlaybackRegionHeadAndTailTime{
            p->ara_dc_id_,
            reinterpret_cast<uint64_t>(playbackRegionRef)});
    if (headTime)
        *headTime = response.head_time;
    if (tailTime)
        *tailTime = response.tail_time;
}

ARA::ARABool ARA_CALL AraDocumentControllerProxy::restore_objects_from_archive(
    ARA::ARADocumentControllerRef r,
    ARA::ARAArchiveReaderHostRef archiveReaderHostRef,
    const ARA::ARARestoreObjectsFilter* filter) {
    auto* p = self(r);

    std::optional<YaAraRestoreObjectsFilter> ya_filter{};
    if (filter) {
        YaAraRestoreObjectsFilter f{};
        f.document_data = static_cast<int32_t>(filter->documentData);
        if (filter->audioSourceArchiveIDs) {
            for (ARA::ARASize i = 0; i < filter->audioSourceIDsCount; ++i)
                f.audio_source_archive_ids.push_back(
                    filter->audioSourceArchiveIDs[i]
                        ? filter->audioSourceArchiveIDs[i]
                        : "");
        }
        if (filter->audioSourceCurrentIDs) {
            for (ARA::ARASize i = 0; i < filter->audioSourceIDsCount; ++i)
                f.audio_source_current_ids.push_back(
                    filter->audioSourceCurrentIDs[i]
                        ? filter->audioSourceCurrentIDs[i]
                        : "");
        }
        if (filter->audioModificationArchiveIDs) {
            for (ARA::ARASize i = 0; i < filter->audioModificationIDsCount; ++i)
                f.audio_modification_archive_ids.push_back(
                    filter->audioModificationArchiveIDs[i]
                        ? filter->audioModificationArchiveIDs[i]
                        : "");
        }
        if (filter->audioModificationCurrentIDs) {
            for (ARA::ARASize i = 0; i < filter->audioModificationIDsCount; ++i)
                f.audio_modification_current_ids.push_back(
                    filter->audioModificationCurrentIDs[i]
                        ? filter->audioModificationCurrentIDs[i]
                        : "");
        }
        ya_filter = std::move(f);
    }

    auto result = p->bridge_.send_message(YaAra::RestoreObjectsFromArchive{
        p->ara_dc_id_,
        reinterpret_cast<uint64_t>(archiveReaderHostRef),
        std::move(ya_filter)});
    return std::visit(
        [](auto&& v) -> ARA::ARABool {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>, int32_t>)
                return static_cast<ARA::ARABool>(v);
            else
                return ARA::kARAFalse;
        },
        result);
}

ARA::ARABool ARA_CALL AraDocumentControllerProxy::store_objects_to_archive(
    ARA::ARADocumentControllerRef r,
    ARA::ARAArchiveWriterHostRef archiveWriterHostRef,
    const ARA::ARAStoreObjectsFilter* filter) {
    auto* p = self(r);

    std::optional<YaAraStoreObjectsFilter> ya_filter{};
    if (filter) {
        YaAraStoreObjectsFilter f{};
        f.document_data = static_cast<int32_t>(filter->documentData);
        if (filter->audioSourceRefs) {
            for (ARA::ARASize i = 0; i < filter->audioSourceRefsCount; ++i)
                f.audio_source_refs.push_back(
                    reinterpret_cast<uint64_t>(filter->audioSourceRefs[i]));
        }
        if (filter->audioModificationRefs) {
            for (ARA::ARASize i = 0; i < filter->audioModificationRefsCount; ++i)
                f.audio_modification_refs.push_back(reinterpret_cast<uint64_t>(
                    filter->audioModificationRefs[i]));
        }
        ya_filter = std::move(f);
    }

    auto result = p->bridge_.send_message(YaAra::StoreObjectsToArchive{
        p->ara_dc_id_,
        reinterpret_cast<uint64_t>(archiveWriterHostRef),
        std::move(ya_filter)});
    return std::visit(
        [](auto&& v) -> ARA::ARABool {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>, int32_t>)
                return static_cast<ARA::ARABool>(v);
            else
                return ARA::kARAFalse;
        },
        result);
}


ARA::ARAInt32 ARA_CALL
AraDocumentControllerProxy::get_processing_algorithms_count(
    ARA::ARADocumentControllerRef /*r*/) {
    return 0;
}

const ARA::ARAProcessingAlgorithmProperties* ARA_CALL
AraDocumentControllerProxy::get_processing_algorithm_properties(
    ARA::ARADocumentControllerRef /*r*/,
    ARA::ARAInt32 /*algorithmIndex*/) {
    return nullptr;
}

ARA::ARAInt32 ARA_CALL
AraDocumentControllerProxy::get_processing_algorithm_for_audio_source(
    ARA::ARADocumentControllerRef /*r*/,
    ARA::ARAAudioSourceRef /*audioSourceRef*/) {
    return 0;
}

void ARA_CALL
AraDocumentControllerProxy::request_processing_algorithm_for_audio_source(
    ARA::ARADocumentControllerRef /*r*/,
    ARA::ARAAudioSourceRef /*audioSourceRef*/,
    ARA::ARAInt32 /*algorithmIndex*/) {}

ARA::ARABool ARA_CALL AraDocumentControllerProxy::is_licensed_for_capabilities(
    ARA::ARADocumentControllerRef /*r*/,
    ARA::ARABool /*runModalActivationDialogIfNeeded*/,
    ARA::ARASize /*contentTypesCount*/,
    const ARA::ARAContentType[] /*contentTypes*/,
    ARA::ARAPlaybackTransformationFlags /*transformationFlags*/) {
    return ARA::kARATrue;
}

ARA::ARABool ARA_CALL
AraDocumentControllerProxy::store_audio_source_to_audio_file_chunk(
    ARA::ARADocumentControllerRef /*r*/,
    ARA::ARAArchiveWriterHostRef /*archiveWriterHostRef*/,
    ARA::ARAAudioSourceRef /*audioSourceRef*/,
    ARA::ARAPersistentID* /*documentArchiveID*/,
    ARA::ARABool* /*openAutomatically*/) {
    return ARA::kARAFalse;
}

ARA::ARABool ARA_CALL
AraDocumentControllerProxy::is_audio_modification_preserving_audio_source_signal(
    ARA::ARADocumentControllerRef /*r*/,
    ARA::ARAAudioModificationRef /*audioModificationRef*/) {
    return ARA::kARAFalse;
}

#endif  // WITH_ARA
