// yabridge: a Wine VST bridge
// Copyright (C) 2020  Robbert van der Helm
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

#include "logging.h"

#ifdef __WINE__
#include "../wine-host/boost-fix.h"
#endif

#include <vestige/aeffectx.h>

#include <boost/process/environment.hpp>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>

#include "vst24.h"

/**
 * The environment variable indicating whether to log to a file. Will log to
 * STDERR if not specified.
 */
constexpr char logging_file_environment_variable[] = "YABRIDGE_DEBUG_FILE";

/**
 * The verbosity of the logging, defaults to `Logger::Verbosity::basic`.
 *
 * @see Logger::Verbosity
 */
constexpr char logging_verbosity_environment_variable[] =
    "YABRIDGE_DEBUG_LEVEL";

Logger::Logger(std::shared_ptr<std::ostream> stream,
               Verbosity verbosity_level,
               std::string prefix)
    : stream(stream), verbosity(verbosity_level), prefix(prefix) {}

Logger Logger::create_from_environment(std::string prefix) {
    auto env = boost::this_process::environment();
    std::string file_path = env[logging_file_environment_variable].to_string();
    std::string verbosity =
        env[logging_verbosity_environment_variable].to_string();

    // Default to `Verbosity::basic` if the environment variable has not
    // been set or if it is not an integer.
    Verbosity verbosity_level;
    try {
        verbosity_level = static_cast<Verbosity>(std::stoi(verbosity));
    } catch (const std::invalid_argument&) {
        verbosity_level = Verbosity::basic;
    }

    // If `file` points to a valid location then use create/truncate the
    // file and write all of the logs there, otherwise use STDERR
    auto log_file = std::make_shared<std::ofstream>(
        file_path, std::fstream::out | std::fstream::app);
    if (log_file->is_open()) {
        return Logger(log_file, verbosity_level, prefix);
    } else {
        return Logger(std::shared_ptr<std::ostream>(&std::cerr, [](auto) {}),
                      verbosity_level, prefix);
    }
}

void Logger::log(const std::string& message) {
    const auto current_time = std::chrono::system_clock::now();
    const std::time_t timestamp =
        std::chrono::system_clock::to_time_t(current_time);

    // How did C++ manage to get time formatting libraries without a way to
    // actually get a timestamp in a threadsafe way? `localtime_r` in C++ is not
    // portable but luckily we only have to support GCC anyway.
    std::tm tm;
    localtime_r(&timestamp, &tm);

    std::ostringstream formatted_message;
    formatted_message << std::put_time(&tm, "%T") << " ";
    formatted_message << prefix;
    formatted_message << message;
    // Flushing a stringstream doesn't do anything, but we need to put a
    // linefeed in this string stream rather writing it sprightly to the output
    // stream to prevent two messages from being put on the same row
    formatted_message << std::endl;

    *stream << formatted_message.str() << std::flush;
}

void Logger::log_get_parameter(int index) {
    if (BOOST_UNLIKELY(verbosity >= Verbosity::most_events)) {
        std::ostringstream message;
        message << ">> getParameter() " << index;

        log(message.str());
    }
}

void Logger::log_get_parameter_response(float value) {
    if (BOOST_UNLIKELY(verbosity >= Verbosity::most_events)) {
        std::ostringstream message;
        message << "   getParameter() :: " << value;

        log(message.str());
    }
}

void Logger::log_set_parameter(int index, float value) {
    if (BOOST_UNLIKELY(verbosity >= Verbosity::most_events)) {
        std::ostringstream message;
        message << ">> setParameter() " << index << " = " << value;

        log(message.str());
    }
}

void Logger::log_set_parameter_response() {
    if (BOOST_UNLIKELY(verbosity >= Verbosity::most_events)) {
        log("   setParameter() :: OK");
    }
}

void Logger::log_event(bool is_dispatch,
                       int opcode,
                       int index,
                       intptr_t value,
                       const EventPayload& payload,
                       float option,
                       const std::optional<EventPayload>& value_payload) {
    if (BOOST_UNLIKELY(verbosity >= Verbosity::most_events)) {
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
        if (opcode_name.has_value()) {
            message << opcode_name.value();
        } else {
            message << "<opcode = " << opcode << ">";
        }

        message << "(index = " << index << ", value = " << value
                << ", option = " << option << ", data = ";

        // Only used during `effSetSpeakerArrangement` and
        // `effGetSpeakerArrangement`
        if (value_payload.has_value()) {
            std::visit(
                overload{
                    [&](auto) {},
                    [&](const DynamicSpeakerArrangement& speaker_arrangement) {
                        message << "<" << speaker_arrangement.speakers.size()
                                << " input_speakers>, ";
                    }},
                value_payload.value());
        }

        std::visit(
            overload{
                [&](const std::nullptr_t&) { message << "<nullptr>"; },
                [&](const std::string& s) {
                    if (s.size() < 32) {
                        message << "\"" << s << "\"";
                    } else {
                        // Long strings contain binary data that we probably
                        // don't want to print
                        message << "<" << s.size() << " bytes>";
                    }
                },
                [&](const std::vector<uint8_t>& buffer) {
                    message << "<" << buffer.size() << " byte chunk>";
                },
                [&](const native_size_t& window_id) {
                    message << "<window " << window_id << ">";
                },
                [&](const AEffect&) { message << "<nullptr>"; },
                [&](const DynamicVstEvents& events) {
                    message << "<" << events.events.size() << " midi_events>";
                },
                [&](const DynamicSpeakerArrangement& speaker_arrangement) {
                    message << "<" << speaker_arrangement.speakers.size()
                            << " output_speakers>";
                },
                [&](const WantsChunkBuffer&) {
                    message << "<writable_buffer>";
                },
                [&](const VstIOProperties&) { message << "<io_properties>"; },
                [&](const VstMidiKeyName&) { message << "<key_name>"; },
                [&](const VstParameterProperties&) {
                    message << "<writable_buffer>";
                },
                [&](const WantsVstRect&) { message << "<writable_buffer>"; },
                [&](const WantsVstTimeInfo&) { message << "<nullptr>"; },
                [&](const WantsString&) { message << "<writable_string>"; }},
            payload);

        message << ")";

        log(message.str());
    }
}

void Logger::log_event_response(
    bool is_dispatch,
    int opcode,
    intptr_t return_value,
    const EventResultPayload& payload,
    const std::optional<EventResultPayload>& value_payload) {
    if (BOOST_UNLIKELY(verbosity >= Verbosity::most_events)) {
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
        if (value_payload.has_value()) {
            std::visit(
                overload{
                    [&](auto) {},
                    [&](const DynamicSpeakerArrangement& speaker_arrangement) {
                        message << ", <" << speaker_arrangement.speakers.size()
                                << " input_speakers>";
                    }},
                value_payload.value());
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
                [&](const std::vector<uint8_t>& buffer) {
                    message << ", <" << buffer.size() << " byte chunk>";
                },
                [&](const AEffect&) { message << ", <AEffect_object>"; },
                [&](const DynamicSpeakerArrangement& speaker_arrangement) {
                    message << ", <" << speaker_arrangement.speakers.size()
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
                [&](const VstTimeInfo&) { message << ", <time_info>"; }},
            payload);

        log(message.str());
    }
}

bool Logger::should_filter_event(bool is_dispatch, int opcode) {
    if (verbosity >= Verbosity::all_events) {
        return false;
    }

    // Filter out log messages related to these events by default since they are
    // called tens of times per second
    // TODO: Figure out what opcode 52 is
    if ((is_dispatch && (opcode == effEditIdle || opcode == 52)) ||
        (!is_dispatch && opcode == audioMasterGetTime)) {
        return true;
    }

    return false;
}

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
                return "effGetSpeakerArrangement ";
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
            default:
                return std::nullopt;
                break;
        }
    }
}
