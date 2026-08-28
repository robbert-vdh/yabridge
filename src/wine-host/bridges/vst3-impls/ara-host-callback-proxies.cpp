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

#include "ara-host-callback-proxies.h"
#include "../../use-linux-asio.h"

#include <cstring>

#include <asio/post.hpp>

#include "../vst3.h"

namespace {

// Each host ref stores the AraHostInstanceProxy* cast directly to uintptr_t.
inline AraHostInstanceProxy* proxy_from_audio_access(
    ARA::ARAAudioAccessControllerHostRef h) noexcept {
    return reinterpret_cast<AraHostInstanceProxy*>(h);
}
inline AraHostInstanceProxy* proxy_from_archiving(
    ARA::ARAArchivingControllerHostRef h) noexcept {
    return reinterpret_cast<AraHostInstanceProxy*>(h);
}
inline AraHostInstanceProxy* proxy_from_content_access(
    ARA::ARAContentAccessControllerHostRef h) noexcept {
    return reinterpret_cast<AraHostInstanceProxy*>(h);
}
inline AraHostInstanceProxy* proxy_from_model_update(
    ARA::ARAModelUpdateControllerHostRef h) noexcept {
    return reinterpret_cast<AraHostInstanceProxy*>(h);
}
inline AraHostInstanceProxy* proxy_from_playback(
    ARA::ARAPlaybackControllerHostRef h) noexcept {
    return reinterpret_cast<AraHostInstanceProxy*>(h);
}

inline std::optional<YaAraContentTimeRange> to_ya(
    const ARA::ARAContentTimeRange* r) noexcept {
    if (!r)
        return std::nullopt;
    return YaAraContentTimeRange{r->start, r->duration};
}

}  // namespace

// ---------------------------------------------------------------------------
// AraHostInstanceProxy constructor
// ---------------------------------------------------------------------------

AraHostInstanceProxy::AraHostInstanceProxy(native_size_t ara_dc_id,
                                            Vst3Bridge& bridge) noexcept
    : bridge_(bridge), ara_dc_id_(ara_dc_id) {
    // Audio access interface
    audio_access_iface.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
        ARAAudioAccessControllerInterface, destroyAudioReader);
    audio_access_iface.createAudioReaderForSource = create_audio_reader;
    audio_access_iface.readAudioSamples = read_audio_samples;
    audio_access_iface.destroyAudioReader = destroy_audio_reader;
    audio_access_host_ref =
        reinterpret_cast<ARA::ARAAudioAccessControllerHostRef>(this);

    // Archiving interface
    archiving_iface.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
        ARAArchivingControllerInterface, getDocumentArchiveID);
    archiving_iface.getArchiveSize = get_archive_size;
    archiving_iface.readBytesFromArchive = read_bytes_from_archive;
    archiving_iface.writeBytesToArchive = write_bytes_to_archive;
    archiving_iface.notifyDocumentArchivingProgress =
        notify_document_archiving_progress;
    archiving_iface.notifyDocumentUnarchivingProgress =
        notify_document_unarchiving_progress;
    archiving_iface.getDocumentArchiveID = get_document_archive_id;
    archiving_host_ref =
        reinterpret_cast<ARA::ARAArchivingControllerHostRef>(this);

    // Content access interface
    content_access_iface.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
        ARAContentAccessControllerInterface, destroyContentReader);
    content_access_iface.isMusicalContextContentAvailable =
        is_musical_context_content_available;
    content_access_iface.getMusicalContextContentGrade =
        get_musical_context_content_grade;
    content_access_iface.createMusicalContextContentReader =
        create_musical_context_content_reader;
    content_access_iface.isAudioSourceContentAvailable =
        is_audio_source_content_available;
    content_access_iface.getAudioSourceContentGrade =
        get_audio_source_content_grade;
    content_access_iface.createAudioSourceContentReader =
        create_audio_source_content_reader;
    content_access_iface.getContentReaderEventCount =
        get_content_reader_event_count;
    content_access_iface.getContentReaderDataForEvent =
        get_content_reader_data_for_event;
    content_access_iface.destroyContentReader = destroy_content_reader;
    content_access_host_ref =
        reinterpret_cast<ARA::ARAContentAccessControllerHostRef>(this);

    // Model update interface
    model_update_iface.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
        ARAModelUpdateControllerInterface, notifyDocumentDataChanged);
    model_update_iface.notifyAudioSourceAnalysisProgress =
        notify_audio_source_analysis_progress;
    model_update_iface.notifyAudioSourceContentChanged =
        notify_audio_source_content_changed;
    model_update_iface.notifyAudioModificationContentChanged =
        notify_audio_modification_content_changed;
    model_update_iface.notifyPlaybackRegionContentChanged =
        notify_playback_region_content_changed;
    model_update_iface.notifyDocumentDataChanged = notify_document_data_changed;
    model_update_host_ref =
        reinterpret_cast<ARA::ARAModelUpdateControllerHostRef>(this);

    // Playback interface
    playback_iface.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
        ARAPlaybackControllerInterface, requestEnableCycle);
    playback_iface.requestStartPlayback = request_start_playback;
    playback_iface.requestStopPlayback = request_stop_playback;
    playback_iface.requestSetPlaybackPosition = request_set_playback_position;
    playback_iface.requestSetCycleRange = request_set_cycle_range;
    playback_iface.requestEnableCycle = request_enable_cycle;
    playback_host_ref =
        reinterpret_cast<ARA::ARAPlaybackControllerHostRef>(this);
}

ARA::ARADocumentControllerHostInstance
AraHostInstanceProxy::build_host_instance() const noexcept {
    ARA::ARADocumentControllerHostInstance inst{};
    inst.structSize = ARA_IMPLEMENTED_STRUCT_SIZE(
        ARADocumentControllerHostInstance, playbackControllerInterface);
    inst.audioAccessControllerHostRef = audio_access_host_ref;
    inst.audioAccessControllerInterface = &audio_access_iface;
    inst.archivingControllerHostRef = archiving_host_ref;
    inst.archivingControllerInterface = &archiving_iface;
    inst.contentAccessControllerHostRef = content_access_host_ref;
    inst.contentAccessControllerInterface = &content_access_iface;
    inst.modelUpdateControllerHostRef = model_update_host_ref;
    inst.modelUpdateControllerInterface = &model_update_iface;
    inst.playbackControllerHostRef = playback_host_ref;
    inst.playbackControllerInterface = &playback_iface;
    return inst;
}

// ---------------------------------------------------------------------------
// Audio access trampolines
// ---------------------------------------------------------------------------

ARA::ARAAudioReaderHostRef ARA_CALL AraHostInstanceProxy::create_audio_reader(
    ARA::ARAAudioAccessControllerHostRef h,
    ARA::ARAAudioSourceHostRef source,
    ARA::ARABool use64bit) {
    auto* p = proxy_from_audio_access(h);
    const auto resp = p->bridge_.send_message(
        YaAra::CreateAudioReader{
            p->ara_dc_id_,
            reinterpret_cast<uint64_t>(source),
            static_cast<bool>(use64bit)});
    if (resp.reader_id == 0)
        return nullptr;
    {
        std::lock_guard lock(p->audio_reader_map_mutex_);
        const int32_t ch = resp.shm_config.input_offsets.empty()
            ? 0
            : static_cast<int32_t>(resp.shm_config.input_offsets[0].size());
        p->audio_reader_channel_count_map_.insert_or_assign(resp.reader_id, ch);
        p->audio_reader_use64_map_.insert_or_assign(
            resp.reader_id, static_cast<bool>(use64bit));
        p->audio_reader_shm_buffers_.insert_or_assign(
            resp.reader_id, AudioShmBuffer{resp.shm_config});
        const size_t bytes_per_sample =
            static_cast<bool>(use64bit) ? sizeof(double) : sizeof(float);
        const uint32_t capacity =
            (ch > 0 && bytes_per_sample > 0 && !resp.shm_config.input_offsets.empty())
                ? static_cast<uint32_t>(
                      (resp.shm_config.size / static_cast<uint32_t>(ch)) /
                      bytes_per_sample)
                : 0;
        p->audio_reader_frame_capacity_map_.insert_or_assign(
            resp.reader_id, capacity);
    }
    return reinterpret_cast<ARA::ARAAudioReaderHostRef>(resp.reader_id);
}

ARA::ARABool ARA_CALL AraHostInstanceProxy::read_audio_samples(
    ARA::ARAAudioAccessControllerHostRef h,
    ARA::ARAAudioReaderHostRef reader,
    ARA::ARASamplePosition pos,
    ARA::ARASampleCount count,
    void* const buffers[]) {
    auto* p = proxy_from_audio_access(h);
    const uint64_t id = reinterpret_cast<uint64_t>(reader);
    int32_t ch = 0;
    bool use64 = false;
    AudioShmBuffer* shm = nullptr;
    uint32_t capacity = 0;
    {
        std::lock_guard lock(p->audio_reader_map_mutex_);
        auto ch_it = p->audio_reader_channel_count_map_.find(id);
        if (ch_it == p->audio_reader_channel_count_map_.end())
            return ARA::kARAFalse;
        ch = ch_it->second;
        if (ch == 0)
            return ARA::kARAFalse;
        auto u64_it = p->audio_reader_use64_map_.find(id);
        if (u64_it != p->audio_reader_use64_map_.end())
            use64 = u64_it->second;
        auto shm_it = p->audio_reader_shm_buffers_.find(id);
        if (shm_it != p->audio_reader_shm_buffers_.end())
            shm = &shm_it->second;
        auto cap_it = p->audio_reader_frame_capacity_map_.find(id);
        if (cap_it != p->audio_reader_frame_capacity_map_.end())
            capacity = cap_it->second;
    }
    if (!shm || !buffers)
        return ARA::kARAFalse;
    ARA::ARASampleCount clamped_count = count;
    if (capacity > 0 &&
        static_cast<uint32_t>(clamped_count) > capacity)
        clamped_count = static_cast<ARA::ARASampleCount>(capacity);
    const auto resp = p->bridge_.send_message(YaAra::ReadAudioSamples{
        p->ara_dc_id_,
        id,
        static_cast<int64_t>(pos),
        static_cast<int32_t>(clamped_count),
        use64});
    if (!resp.success)
        return ARA::kARAFalse;
    const size_t bytes_per_sample = use64 ? sizeof(double) : sizeof(float);
    for (int32_t c = 0; c < ch; ++c) {
        if (!buffers[static_cast<size_t>(c)])
            continue;
        const void* src =
            use64
                ? static_cast<const void*>(
                      shm->input_channel_ptr<double>(0, static_cast<uint32_t>(c)))
                : static_cast<const void*>(
                      shm->input_channel_ptr<float>(0, static_cast<uint32_t>(c)));
        std::memcpy(buffers[static_cast<size_t>(c)], src,
                    static_cast<size_t>(clamped_count) * bytes_per_sample);
    }
    return ARA::kARATrue;
}

void ARA_CALL AraHostInstanceProxy::destroy_audio_reader(
    ARA::ARAAudioAccessControllerHostRef h,
    ARA::ARAAudioReaderHostRef reader) {
    auto* p = proxy_from_audio_access(h);
    const uint64_t id = reinterpret_cast<uint64_t>(reader);
    {
        std::lock_guard lock(p->audio_reader_map_mutex_);
        p->audio_reader_channel_count_map_.erase(id);
        p->audio_reader_frame_capacity_map_.erase(id);
        p->audio_reader_use64_map_.erase(id);
        p->audio_reader_shm_buffers_.erase(id);
    }
    p->bridge_.send_message(YaAra::DestroyAudioReader{p->ara_dc_id_, id});
}

// ---------------------------------------------------------------------------
// Archiving trampolines
// ---------------------------------------------------------------------------

ARA::ARASize ARA_CALL AraHostInstanceProxy::get_archive_size(
    ARA::ARAArchivingControllerHostRef h,
    ARA::ARAArchiveReaderHostRef reader) {
    auto* p = proxy_from_archiving(h);
    return static_cast<ARA::ARASize>(
        p->bridge_.send_message(YaAra::HostCallback::GetArchiveSize{
            p->ara_dc_id_, reinterpret_cast<uint64_t>(reader)}).value);
}

ARA::ARABool ARA_CALL AraHostInstanceProxy::read_bytes_from_archive(
    ARA::ARAArchivingControllerHostRef h,
    ARA::ARAArchiveReaderHostRef reader,
    ARA::ARASize position,
    ARA::ARASize length,
    ARA::ARAByte buffer[]) {
    auto* p = proxy_from_archiving(h);
    const auto response =
        p->bridge_.send_message(YaAra::HostCallback::ReadBytesFromArchive{
            p->ara_dc_id_,
            reinterpret_cast<uint64_t>(reader),
            static_cast<uint64_t>(position),
            static_cast<uint64_t>(length)});
    if (response.data.size() != length)
        return ARA::kARAFalse;
    std::copy(response.data.begin(), response.data.end(),
              reinterpret_cast<uint8_t*>(buffer));
    return ARA::kARATrue;
}

ARA::ARABool ARA_CALL AraHostInstanceProxy::write_bytes_to_archive(
    ARA::ARAArchivingControllerHostRef h,
    ARA::ARAArchiveWriterHostRef writer,
    ARA::ARASize position,
    ARA::ARASize length,
    const ARA::ARAByte buffer[]) {
    auto* p = proxy_from_archiving(h);
    std::vector<uint8_t> data(
        reinterpret_cast<const uint8_t*>(buffer),
        reinterpret_cast<const uint8_t*>(buffer) + length);
    const int32_t result =
        p->bridge_.send_message(YaAra::HostCallback::WriteBytesToArchive{
            p->ara_dc_id_,
            reinterpret_cast<uint64_t>(writer),
            static_cast<uint64_t>(position),
            std::move(data)}).value;
    return result ? ARA::kARATrue : ARA::kARAFalse;
}

void ARA_CALL AraHostInstanceProxy::notify_document_archiving_progress(
    ARA::ARAArchivingControllerHostRef h,
    float value) {
    auto* p = proxy_from_archiving(h);
    p->bridge_.send_message(
        YaAra::HostCallback::NotifyDocumentArchivingProgress{
            p->ara_dc_id_, value});
}

void ARA_CALL AraHostInstanceProxy::notify_document_unarchiving_progress(
    ARA::ARAArchivingControllerHostRef h,
    float value) {
    auto* p = proxy_from_archiving(h);
    p->bridge_.send_message(
        YaAra::HostCallback::NotifyDocumentUnarchivingProgress{
            p->ara_dc_id_, value});
}

ARA::ARAPersistentID ARA_CALL AraHostInstanceProxy::get_document_archive_id(
    ARA::ARAArchivingControllerHostRef h,
    ARA::ARAArchiveReaderHostRef reader) {
    auto* p = proxy_from_archiving(h);
    const auto resp =
        p->bridge_.send_message(YaAra::HostCallback::GetDocumentArchiveID{
            p->ara_dc_id_, reinterpret_cast<uint64_t>(reader)});
    std::lock_guard lock(p->last_event_mutex_);
    p->last_archive_id_ = resp.value;
    return p->last_archive_id_.empty() ? nullptr
                                       : p->last_archive_id_.c_str();
}

// ---------------------------------------------------------------------------
// Content access trampolines
// ---------------------------------------------------------------------------

ARA::ARABool ARA_CALL
AraHostInstanceProxy::is_musical_context_content_available(
    ARA::ARAContentAccessControllerHostRef h,
    ARA::ARAMusicalContextHostRef ctx,
    ARA::ARAContentType type) {
    auto* p = proxy_from_content_access(h);
    return static_cast<ARA::ARABool>(
        p->bridge_.send_message(
            YaAra::HostCallback::IsMusicalContextContentAvailable{
                p->ara_dc_id_,
                reinterpret_cast<uint64_t>(ctx),
                static_cast<int32_t>(type)}).value);
}

ARA::ARAContentGrade ARA_CALL
AraHostInstanceProxy::get_musical_context_content_grade(
    ARA::ARAContentAccessControllerHostRef h,
    ARA::ARAMusicalContextHostRef ctx,
    ARA::ARAContentType type) {
    auto* p = proxy_from_content_access(h);
    return static_cast<ARA::ARAContentGrade>(
        p->bridge_.send_message(
            YaAra::HostCallback::GetMusicalContextContentGrade{
                p->ara_dc_id_,
                reinterpret_cast<uint64_t>(ctx),
                static_cast<int32_t>(type)}).value);
}

ARA::ARAContentReaderHostRef ARA_CALL
AraHostInstanceProxy::create_musical_context_content_reader(
    ARA::ARAContentAccessControllerHostRef h,
    ARA::ARAMusicalContextHostRef ctx,
    ARA::ARAContentType type,
    const ARA::ARAContentTimeRange* range) {
    auto* p = proxy_from_content_access(h);
    const uint64_t id = p->bridge_.send_message(
        YaAra::HostCallback::CreateMusicalContextContentReader{
            p->ara_dc_id_,
            reinterpret_cast<uint64_t>(ctx),
            static_cast<int32_t>(type),
            to_ya(range)}).value;
    if (id != 0) {
        std::lock_guard lock(p->content_reader_type_map_mutex_);
        p->content_reader_type_map_.insert_or_assign(id, type);
    }
    return reinterpret_cast<ARA::ARAContentReaderHostRef>(id);
}

ARA::ARABool ARA_CALL AraHostInstanceProxy::is_audio_source_content_available(
    ARA::ARAContentAccessControllerHostRef h,
    ARA::ARAAudioSourceHostRef source,
    ARA::ARAContentType type) {
    auto* p = proxy_from_content_access(h);
    return static_cast<ARA::ARABool>(
        p->bridge_.send_message(
            YaAra::HostCallback::IsAudioSourceContentAvailable{
                p->ara_dc_id_,
                reinterpret_cast<uint64_t>(source),
                static_cast<int32_t>(type)}).value);
}

ARA::ARAContentGrade ARA_CALL
AraHostInstanceProxy::get_audio_source_content_grade(
    ARA::ARAContentAccessControllerHostRef h,
    ARA::ARAAudioSourceHostRef source,
    ARA::ARAContentType type) {
    auto* p = proxy_from_content_access(h);
    return static_cast<ARA::ARAContentGrade>(
        p->bridge_.send_message(
            YaAra::HostCallback::GetAudioSourceContentGrade{
                p->ara_dc_id_,
                reinterpret_cast<uint64_t>(source),
                static_cast<int32_t>(type)}).value);
}

ARA::ARAContentReaderHostRef ARA_CALL
AraHostInstanceProxy::create_audio_source_content_reader(
    ARA::ARAContentAccessControllerHostRef h,
    ARA::ARAAudioSourceHostRef source,
    ARA::ARAContentType type,
    const ARA::ARAContentTimeRange* range) {
    auto* p = proxy_from_content_access(h);
    const uint64_t id = p->bridge_.send_message(
        YaAra::HostCallback::CreateAudioSourceContentReader{
            p->ara_dc_id_,
            reinterpret_cast<uint64_t>(source),
            static_cast<int32_t>(type),
            to_ya(range)}).value;
    if (id != 0) {
        std::lock_guard lock(p->content_reader_type_map_mutex_);
        p->content_reader_type_map_.insert_or_assign(id, type);
    }
    return reinterpret_cast<ARA::ARAContentReaderHostRef>(id);
}

ARA::ARAInt32 ARA_CALL AraHostInstanceProxy::get_content_reader_event_count(
    ARA::ARAContentAccessControllerHostRef h,
    ARA::ARAContentReaderHostRef reader) {
    auto* p = proxy_from_content_access(h);
    return p->bridge_.send_message(
        YaAra::HostCallback::GetContentReaderEventCount{
            p->ara_dc_id_, reinterpret_cast<uint64_t>(reader)}).value;
}

const void* ARA_CALL AraHostInstanceProxy::get_content_reader_data_for_event(
    ARA::ARAContentAccessControllerHostRef h,
    ARA::ARAContentReaderHostRef reader,
    ARA::ARAInt32 index) {
    auto* p = proxy_from_content_access(h);
    const uint64_t id = reinterpret_cast<uint64_t>(reader);
    int32_t content_type = 0;
    {
        std::lock_guard lock(p->content_reader_type_map_mutex_);
        auto type_it = p->content_reader_type_map_.find(id);
        if (type_it != p->content_reader_type_map_.end())
            content_type = static_cast<int32_t>(type_it->second);
    }
    auto resp = p->bridge_.send_message(
        YaAra::HostCallback::GetContentReaderDataForEvent{
            p->ara_dc_id_,
            id,
            index,
            content_type});
    std::lock_guard lock(p->last_event_mutex_);
    p->last_event_data_ = std::move(resp.data);
    if (p->last_event_data_.empty())
        return nullptr;
    const uint8_t* src = p->last_event_data_.data();
    const uint8_t* end = src + p->last_event_data_.size();
    auto read = [&](void* dst, size_t n) -> bool {
        if (src + n > end) return false;
        std::memcpy(dst, src, n);
        src += n;
        return true;
    };
    if (content_type ==
        static_cast<int32_t>(ARA::kARAContentTypeStaticTuning)) {
        p->last_tuning_ = {};
        ARA::ARAContentTuning& t = p->last_tuning_;
        if (!read(&t.concertPitchFrequency, sizeof(t.concertPitchFrequency)) ||
            !read(&t.tunings, sizeof(t.tunings)))
            return nullptr;
        uint32_t name_len = 0;
        if (!read(&name_len, sizeof(name_len)))
            return nullptr;
        if (src + name_len > end)
            return nullptr;
        p->last_chord_name_.assign(reinterpret_cast<const char*>(src), name_len);
        src += name_len;
        t.name = p->last_chord_name_.empty() ? nullptr
                                              : p->last_chord_name_.c_str();
        return &t;
    }
    if (content_type ==
        static_cast<int32_t>(ARA::kARAContentTypeKeySignatures)) {
        p->last_key_sig_ = {};
        ARA::ARAContentKeySignature& k = p->last_key_sig_;
        if (!read(&k.root, sizeof(k.root)) ||
            !read(&k.intervals, sizeof(k.intervals)))
            return nullptr;
        uint32_t name_len = 0;
        if (!read(&name_len, sizeof(name_len)))
            return nullptr;
        if (src + name_len > end)
            return nullptr;
        p->last_chord_name_.assign(reinterpret_cast<const char*>(src), name_len);
        src += name_len;
        k.name = p->last_chord_name_.empty() ? nullptr
                                              : p->last_chord_name_.c_str();
        if (!read(&k.position, sizeof(k.position)))
            return nullptr;
        return &k;
    }
    if (content_type ==
        static_cast<int32_t>(ARA::kARAContentTypeSheetChords)) {
        ARA::ARAContentChord chord{};
        uint32_t name_len = 0;
        if (!read(&chord.root, sizeof(chord.root)) ||
            !read(&chord.bass, sizeof(chord.bass)) ||
            !read(&chord.intervals, sizeof(chord.intervals)) ||
            !read(&name_len, sizeof(name_len)))
            return nullptr;
        if (src + name_len > end)
            return nullptr;
        p->last_chord_name_.assign(
            reinterpret_cast<const char*>(src), name_len);
        src += name_len;
        chord.name = p->last_chord_name_.empty()
                         ? nullptr
                         : p->last_chord_name_.c_str();
        if (!read(&chord.position, sizeof(chord.position)))
            return nullptr;
        p->last_chord_ = chord;
        return &p->last_chord_;
    }
    return p->last_event_data_.data();
}

void ARA_CALL AraHostInstanceProxy::destroy_content_reader(
    ARA::ARAContentAccessControllerHostRef h,
    ARA::ARAContentReaderHostRef reader) {
    auto* p = proxy_from_content_access(h);
    const uint64_t id = reinterpret_cast<uint64_t>(reader);
    {
        std::lock_guard lock(p->content_reader_type_map_mutex_);
        p->content_reader_type_map_.erase(id);
    }
    p->bridge_.send_message(YaAra::HostCallback::DestroyContentReader{
        p->ara_dc_id_, id});
}

// ---------------------------------------------------------------------------
// Model update trampolines (async fire-and-forget)
// ---------------------------------------------------------------------------

void ARA_CALL AraHostInstanceProxy::notify_audio_source_analysis_progress(
    ARA::ARAModelUpdateControllerHostRef h,
    ARA::ARAAudioSourceHostRef source,
    ARA::ARAAnalysisProgressState state,
    float value) {
    auto* p = proxy_from_model_update(h);
    asio::post(p->bridge_.main_context_.context_,
               [b = &p->bridge_,
                msg = YaAra::HostCallback::NotifyAudioSourceAnalysisProgress{
                    p->ara_dc_id_,
                    reinterpret_cast<uint64_t>(source),
                    static_cast<int32_t>(state),
                    value}]() { b->send_message(msg); });
}

void ARA_CALL AraHostInstanceProxy::notify_audio_source_content_changed(
    ARA::ARAModelUpdateControllerHostRef h,
    ARA::ARAAudioSourceHostRef source,
    const ARA::ARAContentTimeRange* range,
    ARA::ARAContentUpdateFlags flags) {
    auto* p = proxy_from_model_update(h);
    asio::post(p->bridge_.main_context_.context_,
               [b = &p->bridge_,
                msg = YaAra::HostCallback::NotifyAudioSourceContentChanged{
                    p->ara_dc_id_,
                    reinterpret_cast<uint64_t>(source),
                    to_ya(range),
                    static_cast<int32_t>(flags)}]() { b->send_message(msg); });
}

void ARA_CALL AraHostInstanceProxy::notify_audio_modification_content_changed(
    ARA::ARAModelUpdateControllerHostRef h,
    ARA::ARAAudioModificationHostRef mod,
    const ARA::ARAContentTimeRange* range,
    ARA::ARAContentUpdateFlags flags) {
    auto* p = proxy_from_model_update(h);
    asio::post(
        p->bridge_.main_context_.context_,
        [b = &p->bridge_,
         msg = YaAra::HostCallback::NotifyAudioModificationContentChanged{
             p->ara_dc_id_,
             reinterpret_cast<uint64_t>(mod),
             to_ya(range),
             static_cast<int32_t>(flags)}]() { b->send_message(msg); });
}

void ARA_CALL AraHostInstanceProxy::notify_playback_region_content_changed(
    ARA::ARAModelUpdateControllerHostRef h,
    ARA::ARAPlaybackRegionHostRef region,
    const ARA::ARAContentTimeRange* range,
    ARA::ARAContentUpdateFlags flags) {
    auto* p = proxy_from_model_update(h);
    asio::post(
        p->bridge_.main_context_.context_,
        [b = &p->bridge_,
         msg = YaAra::HostCallback::NotifyPlaybackRegionContentChanged{
             p->ara_dc_id_,
             reinterpret_cast<uint64_t>(region),
             to_ya(range),
             static_cast<int32_t>(flags)}]() { b->send_message(msg); });
}

void ARA_CALL AraHostInstanceProxy::notify_document_data_changed(
    ARA::ARAModelUpdateControllerHostRef h) {
    auto* p = proxy_from_model_update(h);
    asio::post(p->bridge_.main_context_.context_,
               [b = &p->bridge_,
                msg = YaAra::HostCallback::NotifyDocumentDataChanged{
                    p->ara_dc_id_}]() { b->send_message(msg); });
}

// ---------------------------------------------------------------------------
// Playback trampolines
// ---------------------------------------------------------------------------

void ARA_CALL AraHostInstanceProxy::request_start_playback(
    ARA::ARAPlaybackControllerHostRef h) {
    auto* p = proxy_from_playback(h);
    p->bridge_.send_message(
        YaAra::HostCallback::RequestStartPlayback{p->ara_dc_id_});
}

void ARA_CALL AraHostInstanceProxy::request_stop_playback(
    ARA::ARAPlaybackControllerHostRef h) {
    auto* p = proxy_from_playback(h);
    p->bridge_.send_message(
        YaAra::HostCallback::RequestStopPlayback{p->ara_dc_id_});
}

void ARA_CALL AraHostInstanceProxy::request_set_playback_position(
    ARA::ARAPlaybackControllerHostRef h,
    ARA::ARATimePosition pos) {
    auto* p = proxy_from_playback(h);
    p->bridge_.send_message(
        YaAra::HostCallback::RequestSetPlaybackPosition{p->ara_dc_id_, pos});
}

void ARA_CALL AraHostInstanceProxy::request_set_cycle_range(
    ARA::ARAPlaybackControllerHostRef h,
    ARA::ARATimePosition start,
    ARA::ARATimeDuration duration) {
    auto* p = proxy_from_playback(h);
    p->bridge_.send_message(YaAra::HostCallback::RequestSetCycleRange{
        p->ara_dc_id_, start, duration});
}

void ARA_CALL AraHostInstanceProxy::request_enable_cycle(
    ARA::ARAPlaybackControllerHostRef h,
    ARA::ARABool enable) {
    auto* p = proxy_from_playback(h);
    p->bridge_.send_message(YaAra::HostCallback::RequestEnableCycle{
        p->ara_dc_id_, static_cast<int32_t>(enable)});
}

// ---------------------------------------------------------------------------
// AraDocumentControllerInstance
// ---------------------------------------------------------------------------

AraDocumentControllerInstance::AraDocumentControllerInstance(
    ARA::ARADocumentControllerRef dc_ref_,
    native_size_t ara_dc_id,
    Vst3Bridge& bridge) noexcept
    : dc_ref(dc_ref_), host_proxy(ara_dc_id, bridge) {}

#endif  // WITH_ARA
