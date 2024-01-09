// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2024 Robbert van der Helm
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

#include "vst2.h"

#include <sstream>

Vst2Logger::Vst2Logger(Logger& generic_logger) : logger_(generic_logger) {}

std::optional<std::string> opcode_to_string(bool is_dispatch, int opcode) {
    if (is_dispatch) {
        // Opcodes for a plugin's dispatch function
        switch (opcode) {
            case effOpen:
                return "effOpen";
                break;
            case effClose:
                return "effClose";
                break;
            case effSetProgram:
                return "effSetProgram";
                break;
            case effGetProgram:
                return "effGetProgram";
                break;
            case effSetProgramName:
                return "effSetProgramName";
                break;
            case effGetProgramName:
                return "effGetProgramName";
                break;
            case effGetParamLabel:
                return "effGetParamLabel";
                break;
            case effGetParamDisplay:
                return "effGetParamDisplay";
                break;
            case effGetParamName:
                return "effGetParamName";
                break;
            case effSetSampleRate:
                return "effSetSampleRate";
                break;
            case effSetBlockSize:
                return "effSetBlockSize";
                break;
            case effMainsChanged:
                return "effMainsChanged";
                break;
            case effEditGetRect:
                return "effEditGetRect";
                break;
            case effEditOpen:
                return "effEditOpen";
                break;
            case effEditClose:
                return "effEditClose";
                break;
            case effEditIdle:
                return "effEditIdle";
                break;
            case effEditTop:
                return "effEditTop";
                break;
            case effIdentify:
                return "effIdentify";
                break;
            case effGetChunk:
                return "effGetChunk";
                break;
            case effSetChunk:
                return "effSetChunk";
                break;
            case effProcessEvents:
                return "effProcessEvents";
                break;
            case effCanBeAutomated:
                return "effCanBeAutomated";
                break;
            case effGetProgramNameIndexed:
                return "effGetProgramNameIndexed";
                break;
            case effGetPlugCategory:
                return "effGetPlugCategory";
                break;
            case effGetEffectName:
                return "effGetEffectName";
                break;
            case effGetParameterProperties:
                return "effGetParameterProperties";
                break;
            case effGetVendorString:
                return "effGetVendorString";
                break;
            case effGetProductString:
                return "effGetProductString";
                break;
            case effGetVendorVersion:
                return "effGetVendorVersion";
                break;
            case effCanDo:
                return "effCanDo";
                break;
            case effIdle:
                return "effIdle";
                break;
            case effGetVstVersion:
                return "effGetVstVersion";
                break;
            case effBeginSetProgram:
                return "effBeginSetProgram";
                break;
            case effEndSetProgram:
                return "effEndSetProgram";
                break;
            case effShellGetNextPlugin:
                return "effShellGetNextPlugin";
                break;
            case effBeginLoadBank:
                return "effBeginLoadBank";
                break;
            case effBeginLoadProgram:
                return "effBeginLoadProgram";
                break;
            case effStartProcess:
                return "effStartProcess";
                break;
            case effStopProcess:
                return "effStopProcess";
                break;
            case effGetInputProperties:
                return "effGetInputProperties";
                break;
            case effGetOutputProperties:
                return "effGetOutputProperties";
                break;
            case effGetMidiKeyName:
                return "effGetMidiKeyName";
                break;
            case effSetSpeakerArrangement:
                return "effSetSpeakerArrangement";
                break;
            case effGetSpeakerArrangement:
                return "effGetSpeakerArrangement";
                break;
            case effString2Parameter:
                return "effString2Parameter";
                break;
            case effVendorSpecific:
                return "effVendorSpecific";
                break;
            case effGetTailSize:
                return "effGetTailSize";
                break;
            case effSetProcessPrecision:
                return "effSetProcessPrecision";
                break;
            default:
                return std::nullopt;
                break;
        }
    } else {
        // Opcodes for the host callback
        switch (opcode) {
            case audioMasterAutomate:
                return "audioMasterAutomate";
                break;
            case audioMasterVersion:
                return "audioMasterVersion";
                break;
            case audioMasterCurrentId:
                return "audioMasterCurrentId";
                break;
            case audioMasterIdle:
                return "audioMasterIdle";
                break;
            case audioMasterPinConnected:
                return "audioMasterPinConnected";
                break;
            case audioMasterWantMidi:
                return "audioMasterWantMidi";
                break;
            case audioMasterGetTime:
                return "audioMasterGetTime";
                break;
            case audioMasterProcessEvents:
                return "audioMasterProcessEvents";
                break;
            case audioMasterSetTime:
                return "audioMasterSetTime";
                break;
            case audioMasterTempoAt:
                return "audioMasterTempoAt";
                break;
            case audioMasterGetNumAutomatableParameters:
                return "audioMasterGetNumAutomatableParameters";
                break;
            case audioMasterGetParameterQuantization:
                return "audioMasterGetParameterQuantization";
                break;
            case audioMasterIOChanged:
                return "audioMasterIOChanged";
                break;
            case audioMasterNeedIdle:
                return "audioMasterNeedIdle";
                break;
            case audioMasterSizeWindow:
                return "audioMasterSizeWindow";
                break;
            case audioMasterGetSampleRate:
                return "audioMasterGetSampleRate";
                break;
            case audioMasterGetBlockSize:
                return "audioMasterGetBlockSize";
                break;
            case audioMasterGetInputLatency:
                return "audioMasterGetInputLatency";
                break;
            case audioMasterGetOutputLatency:
                return "audioMasterGetOutputLatency";
                break;
            case audioMasterGetPreviousPlug:
                return "audioMasterGetPreviousPlug";
                break;
            case audioMasterGetNextPlug:
                return "audioMasterGetNextPlug";
                break;
            case audioMasterWillReplaceOrAccumulate:
                return "audioMasterWillReplaceOrAccumulate";
                break;
            case audioMasterGetCurrentProcessLevel:
                return "audioMasterGetCurrentProcessLevel";
                break;
            case audioMasterGetAutomationState:
                return "audioMasterGetAutomationState";
                break;
            case audioMasterOfflineStart:
                return "audioMasterOfflineStart";
                break;
            case audioMasterOfflineRead:
                return "audioMasterOfflineRead";
                break;
            case audioMasterOfflineWrite:
                return "audioMasterOfflineWrite";
                break;
            case audioMasterOfflineGetCurrentPass:
                return "audioMasterOfflineGetCurrentPass";
                break;
            case audioMasterOfflineGetCurrentMetaPass:
                return "audioMasterOfflineGetCurrentMetaPass";
                break;
            case audioMasterSetOutputSampleRate:
                return "audioMasterSetOutputSampleRate";
                break;
            case audioMasterGetSpeakerArrangement:
                return "audioMasterGetSpeakerArrangement";
                break;
            case audioMasterGetVendorString:
                return "audioMasterGetVendorString";
                break;
            case audioMasterGetProductString:
                return "audioMasterGetProductString";
                break;
            case audioMasterGetVendorVersion:
                return "audioMasterGetVendorVersion";
                break;
            case audioMasterVendorSpecific:
                return "audioMasterVendorSpecific";
                break;
            case audioMasterSetIcon:
                return "audioMasterSetIcon";
                break;
            case audioMasterCanDo:
                return "audioMasterCanDo";
                break;
            case audioMasterGetLanguage:
                return "audioMasterGetLanguage";
                break;
            case audioMasterOpenWindow:
                return "audioMasterOpenWindow";
                break;
            case audioMasterCloseWindow:
                return "audioMasterCloseWindow";
                break;
            case audioMasterGetDirectory:
                return "audioMasterGetDirectory";
                break;
            case audioMasterUpdateDisplay:
                return "audioMasterUpdateDisplay";
                break;
            case audioMasterBeginEdit:
                return "audioMasterBeginEdit";
                break;
            case audioMasterEndEdit:
                return "audioMasterEndEdit";
                break;
            case audioMasterOpenFileSelector:
                return "audioMasterOpenFileSelector";
                break;
            case audioMasterCloseFileSelector:
                return "audioMasterCloseFileSelector";
                break;
            case audioMasterEditFile:
                return "audioMasterEditFile";
                break;
            case audioMasterGetChunkFile:
                return "audioMasterGetChunkFile";
                break;
            case audioMasterGetInputSpeakerArrangement:
                return "audioMasterGetInputSpeakerArrangement";
                break;
            case audioMasterDeadBeef:
                return "0xdeadbeef";
                break;
            default:
                return std::nullopt;
                break;
        }
    }
}

void Vst2Logger::log_get_parameter(int index) {
    if (logger_.verbosity_ >= Logger::Verbosity::most_events) [[unlikely]] {
        std::ostringstream message;
        message << ">> getParameter() " << index;

        log(message.str());
    }
}

void Vst2Logger::log_get_parameter_response(float value) {
    if (logger_.verbosity_ >= Logger::Verbosity::most_events) [[unlikely]] {
        std::ostringstream message;
        message << "   getParameter() :: " << value;

        log(message.str());
    }
}

void Vst2Logger::log_set_parameter(int index, float value) {
    if (logger_.verbosity_ >= Logger::Verbosity::most_events) [[unlikely]] {
        std::ostringstream message;
        message << ">> setParameter() " << index << " = " << value;

        log(message.str());
    }
}

void Vst2Logger::log_set_parameter_response() {
    if (logger_.verbosity_ >= Logger::Verbosity::most_events) [[unlikely]] {
        log("   setParameter() :: OK");
    }
}

void Vst2Logger::log_event(
    bool is_dispatch,
    int opcode,
    int index,
    intptr_t value,
    const Vst2Event::Payload& payload,
    float option,
    const std::optional<Vst2Event::Payload>& value_payload) {
    if (logger_.verbosity_ >= Logger::Verbosity::most_events) [[unlikely]] {
        if (should_filter_event(is_dispatch, opcode)) {
            return;
        }

        std::ostringstream message;
        if (is_dispatch) {
            message << ">> dispatch() ";
        } else {
            message << ">> audioMasterCallback() ";
        }

        const auto opcode_name = opcode_to_string(is_dispatch, opcode);
        if (opcode_name) {
            message << *opcode_name;
        } else {
            message << "<opcode = " << opcode << ">";
        }

        message << "(index = " << index << ", value = " << value
                << ", option = " << option << ", data = ";

        // Only used during `effSetSpeakerArrangement` and
        // `effGetSpeakerArrangement`
        if (value_payload) {
            std::visit(
                overload{
                    [](const auto&) {},
                    [&](const DynamicSpeakerArrangement& speaker_arrangement) {
                        message << "<" << speaker_arrangement.speakers_.size()
                                << " input_speakers>, ";
                    }},
                *value_payload);
        }

        std::visit(
            overload{
                [&](const std::nullptr_t&) { message << "nullptr"; },
                [&](const std::string& s) {
                    if (s.size() < 32) {
                        message << "\"" << s << "\"";
                    } else {
                        // Long strings contain binary data that we probably
                        // don't want to print
                        message << "<" << s.size() << " bytes>";
                    }
                },
                [&](const ChunkData& chunk) {
                    message << "<" << chunk.buffer.size() << " byte chunk>";
                },
                [&](const native_size_t& window_id) {
                    message << "<window " << window_id << ">";
                },
                [&](const AEffect&) { message << "nullptr"; },
                [&](const DynamicVstEvents& events) {
                    message << "<" << events.events_.size() << " midi_events";
                    if (!events.sysex_data_.empty()) {
                        message << ", including " << events.sysex_data_.size()
                                << " sysex_events>";
                    } else {
                        message << ">";
                    }
                },
                [&](const DynamicSpeakerArrangement& speaker_arrangement) {
                    message << "<" << speaker_arrangement.speakers_.size()
                            << " output_speakers>";
                },
                [&](const VstIOProperties&) { message << "<io_properties>"; },
                [&](const VstMidiKeyName&) { message << "<key_name>"; },
                [&](const VstParameterProperties&) {
                    message << "<writable_buffer>";
                },
                [&](const VstPatchChunkInfo& info) {
                    message << "<patch_chunk_info for " << info.numElements
                            << " banks/programs>";
                },
                [&](const WantsAEffectUpdate&) { message << "nullptr"; },
                [&](const WantsAudioShmBufferConfig&) { message << "nullptr"; },
                [&](const WantsChunkBuffer&) {
                    message << "<writable_buffer>";
                },
                [&](const WantsVstRect&) { message << "VstRect**"; },
                [&](const WantsVstTimeInfo&) { message << "nullptr"; },
                [&](const WantsString&) { message << "<writable_string>"; }},
            payload);

        message << ")";

        log(message.str());
    }
}

void Vst2Logger::log_event_response(
    bool is_dispatch,
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    int opcode,
    intptr_t return_value,
    const Vst2EventResult::Payload& payload,
    const std::optional<Vst2EventResult::Payload>& value_payload,
    bool from_cache) {
    if (logger_.verbosity_ >= Logger::Verbosity::most_events) [[unlikely]] {
        if (should_filter_event(is_dispatch, opcode)) {
            return;
        }

        std::ostringstream message;
        if (is_dispatch) {
            message << "   dispatch() :: ";
        } else {
            message << "   audioMasterCallback() :: ";
        }

        message << return_value;

        // Only used during `effSetSpeakerArrangement` and
        // `effGetSpeakerArrangement`
        if (value_payload) {
            std::visit(
                overload{
                    [](const auto&) {},
                    [&](const DynamicSpeakerArrangement& speaker_arrangement) {
                        message << ", <" << speaker_arrangement.speakers_.size()
                                << " input_speakers>";
                    }},
                *value_payload);
        }

        std::visit(
            overload{
                [&](const std::nullptr_t&) {},
                [&](const std::string& s) {
                    if (s.size() < 32) {
                        message << ", \"" << s << "\"";
                    } else {
                        // Long strings contain binary data that we probably
                        // don't want to print
                        message << ", <" << s.size() << " bytes>";
                    }
                },
                [&](const ChunkData& chunk) {
                    message << ", <" << chunk.buffer.size() << " byte chunk>";
                },
                [&](const AEffect&) { message << ", <AEffect object>"; },
                [&](const AudioShmBuffer::Config& config) {
                    message << ", <shared memory configuration for \""
                            << config.name << "\", " << config.size
                            << " bytes>";
                },
                [&](const DynamicSpeakerArrangement& speaker_arrangement) {
                    message << ", <" << speaker_arrangement.speakers_.size()
                            << " output_speakers>";
                },
                [&](const VstIOProperties&) { message << ", <io_properties>"; },
                [&](const VstMidiKeyName&) { message << ", <key_name>"; },
                [&](const VstParameterProperties& props) {
                    message << ", <parameter_properties for '" << props.label
                            << "'>";
                },
                [&](const VstRect& rect) {
                    message << ", {l: " << rect.left << ", t: " << rect.top
                            << ", r: " << rect.right << ", b: " << rect.bottom
                            << "}";
                },
                [&](const VstTimeInfo& info) {
                    message << ", <"
                            << "tempo = " << info.tempo << " bpm"
                            << ", quarter_notes = " << info.ppqPos
                            << ", samples = " << info.samplePos << ">";
                }},
            payload);

        if (from_cache) {
            message << " (from cache)";
        }

        log(message.str());
    }
}

bool Vst2Logger::should_filter_event(bool is_dispatch,
                                     int opcode) const noexcept {
    if (logger_.verbosity_ >= Logger::Verbosity::all_events) {
        return false;
    }

    // Filter out log messages related to these events by default since they are
    // called tens of times per second
    if ((is_dispatch && (opcode == effEditIdle || opcode == effGetTailSize ||
                         opcode == effIdle || opcode == effProcessEvents)) ||
        (!is_dispatch && (opcode == audioMasterGetTime ||
                          opcode == audioMasterGetCurrentProcessLevel))) {
        return true;
    }

    return false;
}
