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

#include "vst3.h"

#include <bitset>

#include <public.sdk/source/vst/utility/stringconvert.h>

#include "src/common/serialization/vst3.h"

Vst3Logger::Vst3Logger(Logger& generic_logger) : logger(generic_logger) {}

void Vst3Logger::log_unknown_interface(
    const std::string& where,
    const std::optional<Steinberg::FUID>& uid) {
    if (BOOST_UNLIKELY(logger.verbosity >= Logger::Verbosity::most_events)) {
        std::string uid_string = uid ? format_uid(*uid) : "<unknown_pointer>";

        std::ostringstream message;
        message << "[unknown interface] " << where << ": " << uid_string;

        log(message.str());
    }
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const Vst3PlugViewProxy::Destruct& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        // We don't know what class this instance was originally instantiated
        // as, but it also doesn't really matter
        message << request.owner_instance_id << ": IPlugView::~IPlugView()";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const Vst3PluginProxy::Construct& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << "IPluginFactory::createInstance(cid = "
                << format_uid(Steinberg::FUID::fromTUID(request.cid.data()))
                << ", _iid = ";
        switch (request.requested_interface) {
            case Vst3PluginProxy::Construct::Interface::IComponent:
                message << "IComponent::iid";
                break;
            case Vst3PluginProxy::Construct::Interface::IEditController:
                message << "IEditController::iid";
                break;
        }
        message << ", &obj)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const Vst3PluginProxy::Destruct& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        // We don't know what class this instance was originally instantiated
        // as, but it also doesn't really matter
        message << request.instance_id << ": FUnknown::~FUnknown()";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const Vst3PluginProxy::SetState& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": {IComponent,IEditController}::setState(state = "
                   "<IBStream* containing "
                << request.state.size() << "bytes>)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const Vst3PluginProxy::GetState& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message
            << request.instance_id
            << ": {IComponent,IEditController}::getState(state = <IBStream*>)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaConnectionPoint::Connect& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IConnectionPoint::connect(other = ";
        std::visit(
            overload{[&](const native_size_t& other_instance_id) {
                         message << "<IConnectionPoint* #" << other_instance_id
                                 << ">";
                     },
                     [&](const Vst3ConnectionPointProxy::ConstructArgs&) {
                         message << "<IConnectionPoint* proxy>";
                     }},
            request.other);
        message << ")";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaConnectionPoint::Disconnect& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IConnectionPoint::disconnect(other = ";
        if (request.other_instance_id) {
            message << "<IConnectionPoint* #" << *request.other_instance_id
                    << ">";
        } else {
            message << "<IConnectionPoint* proxy>";
        }
        message << ")";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaConnectionPoint::Notify& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        // We can safely print the pointer as long we don't dereference it
        message << request.instance_id
                << ": IConnectionPoint::notify(message = <IMessage* "
                << request.message_ptr.get_original();
        if (const char* id =
                const_cast<YaMessagePtr&>(request.message_ptr).getMessageID()) {
            message << " with ID = \"" << id << "\"";
        } else {
            message << " without an ID";
        }
        message << ">)";
    });
}

bool Vst3Logger::log_request(
    bool is_host_vst,
    const YaEditController::SetComponentState& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IEditController::setComponentState(state = <IBStream* "
                   "containing "
                << request.state.size() << " bytes>)";
    });
}

bool Vst3Logger::log_request(
    bool is_host_vst,
    const YaEditController::GetParameterCount& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IEditController::getParameterCount()";
    });
}

bool Vst3Logger::log_request(
    bool is_host_vst,
    const YaEditController::GetParameterInfo& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IEditController::getParameterInfo(paramIndex = "
                << request.param_index << ", &info)";
    });
}

bool Vst3Logger::log_request(
    bool is_host_vst,
    const YaEditController::GetParamStringByValue& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IEditController::getParamStringByValue(id = "
                << request.id
                << ", valueNormalized = " << request.value_normalized
                << ", &string)";
    });
}

bool Vst3Logger::log_request(
    bool is_host_vst,
    const YaEditController::GetParamValueByString& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        std::string param_title = VST3::StringConvert::convert(request.string);
        message << request.instance_id
                << ": IEditController::getParamValueByString(id = "
                << request.id << ", string = " << param_title
                << ", &valueNormalized)";
    });
}

bool Vst3Logger::log_request(
    bool is_host_vst,
    const YaEditController::NormalizedParamToPlain& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IEditController::normalizedParamToPlain(id = "
                << request.id
                << ", valueNormalized = " << request.value_normalized << ")";
    });
}

bool Vst3Logger::log_request(
    bool is_host_vst,
    const YaEditController::PlainParamToNormalized& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IEditController::plainParamToNormalized(id = "
                << request.id << ", plainValue = " << request.plain_value
                << ")";
    });
}

bool Vst3Logger::log_request(
    bool is_host_vst,
    const YaEditController::GetParamNormalized& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IEditController::getParamNormalized(id = " << request.id
                << ")";
    });
}

bool Vst3Logger::log_request(
    bool is_host_vst,
    const YaEditController::SetParamNormalized& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IEditController::setParamNormalized(id = " << request.id
                << ", value = " << request.value << ")";
    });
}

bool Vst3Logger::log_request(
    bool is_host_vst,
    const YaEditController::SetComponentHandler& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IEditController::setComponentHandler(handler = "
                   "<IComponentHandler*>)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaEditController::CreateView& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IEditController::createView(name = \"" << request.name
                << "\")";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaEditController2::SetKnobMode& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IEditController2::setKnobMode(mode = " << request.mode
                << ")";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaEditController2::OpenHelp& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IEditController2::openHelp(onlyCheck = "
                << (request.only_check ? "true" : "false") << ")";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaEditController2::OpenAboutBox& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IEditController2::openAboutBox(onlyCheck = "
                << (request.only_check ? "true" : "false") << ")";
    });
}

bool Vst3Logger::log_request(
    bool is_host_vst,
    const YaPlugView::IsPlatformTypeSupported& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.owner_instance_id
                << ": IPlugView::isPLatformTypeSupported(type = \""
                << request.type;
        if (request.type == Steinberg::kPlatformTypeX11EmbedWindowID) {
            message << "\" (will be translated to \""
                    << Steinberg::kPlatformTypeHWND << "\")";
        }
        message << ")";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaPlugView::Attached& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.owner_instance_id
                << ": IPlugView::attached(parent = " << request.parent
                << ", type = \"" << request.type;
        if (request.type == Steinberg::kPlatformTypeX11EmbedWindowID) {
            message << "\" (will be translated to \""
                    << Steinberg::kPlatformTypeHWND << "\")";
        }
        message << ")";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaPlugView::Removed& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.owner_instance_id << ": IPlugView::removed()";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaPlugView::OnWheel& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.owner_instance_id
                << ": IPlugView::onWheel(distance = " << request.distance
                << ")";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaPlugView::OnKeyDown& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        // This static cast is technically not correct of course but it's
        // UTF-16, so everything's allowed
        message << request.owner_instance_id << ": IPlugView::onKeyDown(key = "
                << static_cast<char>(request.key)
                << ", keyCode = " << request.key_code
                << ", modifiers = " << request.modifiers << ")";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaPlugView::OnKeyUp& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        // This static cast is technically not correct of course but it's
        // UTF-16, so everything's allowed
        message << request.owner_instance_id << ": IPlugView::onKeyUp(key = "
                << static_cast<char>(request.key)
                << ", keyCode = " << request.key_code
                << ", modifiers = " << request.modifiers << ")";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaPlugView::GetSize& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.owner_instance_id << ": IPlugView::getSize(size*)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaPlugView::OnSize& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.owner_instance_id
                << ": IPlugView::onSize(newSize = <ViewRect* with left = "
                << request.new_size.left << ", top = " << request.new_size.top
                << ", right = " << request.new_size.right
                << ", bottom = " << request.new_size.bottom << ">)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaPlugView::OnFocus& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.owner_instance_id << ": IPlugView::onFucus(state = "
                << (request.state ? "true" : "false") << ")";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaPlugView::SetFrame& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.owner_instance_id
                << ": IPlugView::setFrame(frame = <IPlugFrame*>)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaPlugView::CanResize& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.owner_instance_id << ": IPlugView::canResize()";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaPlugView::CheckSizeConstraint& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.owner_instance_id
                << ": IPlugView::checkSizeConstraint(rect = "
                   "<ViewRect* with left = "
                << request.rect.left << ", top = " << request.rect.top
                << ", right = " << request.rect.right
                << ", bottom = " << request.rect.bottom << ">)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaPluginBase::Initialize& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IPluginBase::initialize(context = <FUnknown*>)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaPluginBase::Terminate& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id << ": IPluginBase::terminate()";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaPluginFactory::Construct&) {
    return log_request_base(
        is_host_vst, [&](auto& message) { message << "GetPluginFactory()"; });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaPluginFactory::SetHostContext&) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << "IPluginFactory3::setHostContext(context = <FUnknown*>)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaProgramListData::ProgramDataSupported&) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << "IProgramListData::programDataSupported()";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaProgramListData::GetProgramData& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << "IProgramListData::getProgramData(listId = "
                << request.list_id
                << ", programIndex = " << request.program_index << ", &data)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaProgramListData::SetProgramData& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << "IProgramListData::setProgramData(listId = "
                << request.list_id
                << ", programIndex = " << request.program_index
                << ", data = <IBStream* containing " << request.data.size()
                << "bytes>)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaUnitData::UnitDataSupported&) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << "IUnitData::unitDataSupported()";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaUnitData::GetUnitData& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << "IUnitData::getUnitData(listId = " << request.unit_id
                << ", &data)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaUnitData::SetUnitData& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << "IUnitData::setUnitData(listId = " << request.unit_id
                << ", data = <IBStream* containing " << request.data.size()
                << "bytes>)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaUnitInfo::GetUnitCount& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id << ": IUnitInfo::getUnitCount()";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaUnitInfo::GetUnitInfo& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IUnitInfo::getUnitInfo(unitIndex = " << request.unit_index
                << ", &info)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaUnitInfo::GetProgramListCount& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id << ": IUnitInfo::getProgramListCount()";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaUnitInfo::GetProgramListInfo& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IUnitInfo::getProgramListInfo(listIndex = "
                << request.list_index << ", &info)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaUnitInfo::GetProgramName& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IUnitInfo::getProgramName(listId = " << request.list_id
                << ", programIndex = " << request.program_index << ", &name)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaUnitInfo::GetProgramInfo& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IUnitInfo::getProgramInfo(listId = " << request.list_id
                << ", programIndex = " << request.program_index
                << ", attributeId = " << request.attribute_id
                << ", &attributeValue)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaUnitInfo::HasProgramPitchNames& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IUnitInfo::hasProgramPitchNames(listId = "
                << request.list_id
                << ", programIndex = " << request.program_index << ")";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaUnitInfo::GetProgramPitchName& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IUnitInfo::getProgramPitchName(listId = "
                << request.list_id
                << ", programIndex = " << request.program_index
                << ", midiPitch = " << request.midi_pitch << ", &name)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaUnitInfo::GetSelectedUnit& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id << ": IUnitInfo::getSelectedUnit()";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaUnitInfo::SelectUnit& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IUnitInfo::selectUnit(unitId = " << request.unit_id
                << ")";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaUnitInfo::GetUnitByBus& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IUnitInfo::getUnitByBus(type = " << request.type
                << ", dir = " << request.dir
                << ", busIndex = " << request.bus_index
                << ", channel = " << request.channel << ", &unitId)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaUnitInfo::SetUnitProgramData& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IUnitInfo::setUnitProgramData(listOrUnitId = "
                << request.list_or_unit_id
                << ", programIndex = " << request.program_index
                << ", data = <IBStream* containing " << request.data.size()
                << "bytes>)";
    });
}

bool Vst3Logger::log_request(
    bool is_host_vst,
    const YaAudioProcessor::SetBusArrangements& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IAudioProcessor::setBusArrangements(inputs = "
                   "[";

        for (bool first = true; const auto& arrangement : request.inputs) {
            if (!first) {
                message << ", ";
            }
            message << "SpeakerArrangement: 0b"
                    << std::bitset<sizeof(Steinberg::Vst::SpeakerArrangement)>(
                           arrangement);
            first = false;
        }

        message << "], numIns = " << request.num_ins << ", outputs = [";

        for (bool first = true; const auto& arrangement : request.outputs) {
            if (!first) {
                message << ", ";
            }
            message << "SpeakerArrangement: 0b"
                    << std::bitset<sizeof(Steinberg::Vst::SpeakerArrangement)>(
                           arrangement);
            first = false;
        }

        message << "], numOuts = " << request.num_outs << ")";
    });
}

bool Vst3Logger::log_request(
    bool is_host_vst,
    const YaAudioProcessor::GetBusArrangement& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IAudioProcessor::getBusArrangement(dir = " << request.dir
                << ", index = " << request.index << ", &arr)";
    });
}

bool Vst3Logger::log_request(
    bool is_host_vst,
    const YaAudioProcessor::CanProcessSampleSize& request) {
    return log_request_base(
        is_host_vst, Logger::Verbosity::all_events, [&](auto& message) {
            message
                << request.instance_id
                << ": IAudioProcessor::canProcessSampleSize(symbolicSampleSize "
                   "= "
                << request.symbolic_sample_size << ")";
        });
}

bool Vst3Logger::log_request(
    bool is_host_vst,
    const YaAudioProcessor::GetLatencySamples& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IAudioProcessor::getLatencySamples()";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaAudioProcessor::SetupProcessing& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IAudioProcessor::setupProcessing(setup = "
                   "<SetupProcessing with mode = "
                << request.setup.processMode << ", symbolic_sample_size = "
                << request.setup.symbolicSampleSize
                << ", max_buffer_size = " << request.setup.maxSamplesPerBlock
                << " and sample_rate = " << request.setup.sampleRate << ">)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaAudioProcessor::SetProcessing& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IAudioProcessor::setProcessing(state = "
                << (request.state ? "true" : "false") << ")";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaAudioProcessor::Process& request) {
    return log_request_base(
        is_host_vst, Logger::Verbosity::all_events, [&](auto& message) {
            // This is incredibly verbose, but if you're really a plugin that
            // handles processing in a weird way you're going to need all of
            // this

            std::ostringstream num_input_channels;
            num_input_channels << "[";
            for (bool is_first = true;
                 const auto& buffers : request.data.inputs) {
                num_input_channels << (is_first ? "" : ", ")
                                   << buffers.num_channels();
                is_first = false;
            }
            num_input_channels << "]";

            std::ostringstream num_output_channels;
            num_output_channels << "[";
            for (bool is_first = true;
                 const auto& num_channels : request.data.outputs_num_channels) {
                num_output_channels << (is_first ? "" : ", ") << num_channels;
                is_first = false;
            }
            num_output_channels << "]";

            message << request.instance_id
                    << ": IAudioProcessor::process(data = <ProcessData with "
                       "input_channels = "
                    << num_input_channels.str()
                    << ", output_channels = " << num_output_channels.str()
                    << ", num_samples = " << request.data.num_samples
                    << ", input_parameter_changes = <IParameterChanges* for "
                    << request.data.input_parameter_changes.num_parameters()
                    << " parameters>, output_parameter_changes = "
                    << (request.data.output_parameter_changes_supported
                            ? "<IParameterChanges*>"
                            : "nullptr")
                    << ", input_events = ";
            if (request.data.input_events) {
                message << "<IEventList* with "
                        << request.data.input_events->num_events()
                        << " events>";
            } else {
                message << "<nullptr>";
            }
            message << ", output_events = "
                    << (request.data.output_events_supported ? "<IEventList*>"
                                                             : "<nullptr>")
                    << ", process_context = "
                    << (request.data.process_context ? "<ProcessContext*>"
                                                     : "<nullptr>")
                    << ", process_mode = " << request.data.process_mode
                    << ", symbolic_sample_size = "
                    << request.data.symbolic_sample_size << ">)";
        });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaAudioProcessor::GetTailSamples& request) {
    return log_request_base(
        is_host_vst, Logger::Verbosity::all_events, [&](auto& message) {
            message << request.instance_id
                    << ": IAudioProcessor::getTailSamples()";
        });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaComponent::GetControllerClassId& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IComponent::getControllerClassId(&classId)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaComponent::SetIoMode& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IComponent::setIoMode(mode = " << request.mode << ")";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaComponent::GetBusCount& request) {
    // JUCE-based hosts will call this every processing cycle, for some reason
    // (it shouldn't be allwoed to change during processing, right?)
    return log_request_base(
        is_host_vst, Logger::Verbosity::all_events, [&](auto& message) {
            message << request.instance_id
                    << ": IComponent::getBusCount(type = " << request.type
                    << ", dir = " << request.dir << ")";
        });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaComponent::GetBusInfo& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IComponent::getBusInfo(type = " << request.type
                << ", dir = " << request.dir << ", index = " << request.index
                << ", &bus)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaComponent::GetRoutingInfo& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message
            << request.instance_id
            << ": IComponent::getRoutingInfo(inInfo = <RoutingInfo& for bus "
            << request.in_info.busIndex << " and channel "
            << request.in_info.channel << ">, outInfo = <RoutingInfo& for bus "
            << request.out_info.busIndex << " and channel "
            << request.out_info.channel << ">)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaComponent::ActivateBus& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id
                << ": IComponent::activateBus(type = " << request.type
                << ", dir = " << request.dir << ", index = " << request.index
                << ", state = " << (request.state ? "true" : "false") << ")";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaComponent::SetActive& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.instance_id << ": IComponent::setActive(state = "
                << (request.state ? "true" : "false") << ")";
    });
}

bool Vst3Logger::log_request(bool is_host_vst, const WantsConfiguration&) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << "Requesting <Configuration>";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaComponentHandler::BeginEdit& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.owner_instance_id
                << ": IComponentHandler::beginEdit(id = " << request.id << ")";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaComponentHandler::PerformEdit& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.owner_instance_id
                << ": IComponentHandler::performEdit(id = " << request.id
                << ", valueNormalized = " << request.value_normalized << ")";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaComponentHandler::EndEdit& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.owner_instance_id
                << ": IComponentHandler::endEdit(id = " << request.id << ")";
    });
}

bool Vst3Logger::log_request(
    bool is_host_vst,
    const YaComponentHandler::RestartComponent& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.owner_instance_id
                << ": IComponentHandler::restartComponent(flags = "
                << request.flags << ")";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaHostApplication::GetName& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        // This can be called either from a plugin object or from the plugin's
        // plugin factory
        if (request.owner_instance_id) {
            message << *request.owner_instance_id << ": ";
        }

        message << "IHostApplication::getName(&name)";
    });
}

bool Vst3Logger::log_request(bool is_host_vst,
                             const YaPlugFrame::ResizeView& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.owner_instance_id
                << ": IPlugFrame::resizeView(view = <IPlugView*>, newSize = "
                   "<ViewRect* with left = "
                << request.new_size.left << ", top = " << request.new_size.top
                << ", right = " << request.new_size.right
                << ", bottom = " << request.new_size.bottom << ">)";
    });
}

bool Vst3Logger::log_request(
    bool is_host_vst,
    const YaUnitHandler::NotifyUnitSelection& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.owner_instance_id
                << ": IUnitHandler::notifyUnitSelection(unitId = "
                << request.unit_id << ")";
    });
}

bool Vst3Logger::log_request(
    bool is_host_vst,
    const YaUnitHandler::NotifyProgramListChange& request) {
    return log_request_base(is_host_vst, [&](auto& message) {
        message << request.owner_instance_id
                << ": IUnitHandler::notifyProgramListChange(listId = "
                << request.list_id
                << ", programIndex = " << request.program_index << ")";
    });
}

void Vst3Logger::log_response(bool is_host_vst, const Ack&) {
    log_response_base(is_host_vst, [&](auto& message) { message << "ACK"; });
}

void Vst3Logger::log_response(bool is_host_vst,
                              const std::variant<Vst3PluginProxy::ConstructArgs,
                                                 UniversalTResult>& result) {
    log_response_base(is_host_vst, [&](auto& message) {
        std::visit(overload{[&](const Vst3PluginProxy::ConstructArgs& args) {
                                message << "<FUnknown* #" << args.instance_id
                                        << ">";
                            },
                            [&](const UniversalTResult& code) {
                                message << code.string();
                            }},
                   result);
    });
}

void Vst3Logger::log_response(
    bool is_host_vst,
    const Vst3PluginProxy::GetStateResponse& response) {
    log_response_base(is_host_vst, [&](auto& message) {
        message << response.result.string();
        if (response.result == Steinberg::kResultOk) {
            message << ", <IBStream* containing "
                    << response.updated_state.size() << " bytes>";
        }
    });
}

void Vst3Logger::log_response(
    bool is_host_vst,
    const YaEditController::GetParameterInfoResponse& response) {
    log_response_base(is_host_vst, [&](auto& message) {
        message << response.result.string();
        if (response.result == Steinberg::kResultOk) {
            std::string param_title =
                VST3::StringConvert::convert(response.updated_info.title);
            message << ", <ParameterInfo for '" << param_title << "'>";
        }
    });
}

void Vst3Logger::log_response(
    bool is_host_vst,
    const YaEditController::GetParamStringByValueResponse& response) {
    log_response_base(is_host_vst, [&](auto& message) {
        message << response.result.string();
        if (response.result == Steinberg::kResultOk) {
            std::string value = VST3::StringConvert::convert(response.string);
            message << ", \"" << value << "\"";
        }
    });
}

void Vst3Logger::log_response(
    bool is_host_vst,
    const YaEditController::GetParamValueByStringResponse& response) {
    log_response_base(is_host_vst, [&](auto& message) {
        message << response.result.string();
        if (response.result == Steinberg::kResultOk) {
            message << ", " << response.value_normalized;
        }
    });
}

void Vst3Logger::log_response(
    bool is_host_vst,
    const YaEditController::CreateViewResponse& response) {
    log_response_base(is_host_vst, [&](auto& message) {
        if (response.plug_view_args) {
            message << "<IPlugView*>";
        } else {
            message << "<nullptr>";
        }
    });
}

void Vst3Logger::log_response(bool is_host_vst,
                              const YaPlugView::GetSizeResponse& response) {
    log_response_base(is_host_vst, [&](auto& message) {
        message << response.result.string();
        if (response.result == Steinberg::kResultOk) {
            message << ", <ViewRect* with left = " << response.updated_size.left
                    << ", top = " << response.updated_size.top
                    << ", right = " << response.updated_size.right
                    << ", bottom = " << response.updated_size.bottom << ">";
        }
    });
}

void Vst3Logger::log_response(
    bool is_host_vst,
    const YaPlugView::CheckSizeConstraintResponse& response) {
    log_response_base(is_host_vst, [&](auto& message) {
        message << response.result.string();
        if (response.result == Steinberg::kResultOk) {
            message << ", <ViewRect* with left = " << response.updated_rect.left
                    << ", top = " << response.updated_rect.top
                    << ", right = " << response.updated_rect.right
                    << ", bottom = " << response.updated_rect.bottom << ">";
        }
    });
}

void Vst3Logger::log_response(bool is_host_vst,
                              const YaPluginFactory::ConstructArgs& args) {
    log_response_base(is_host_vst, [&](auto& message) {
        message << "<IPluginFactory* with " << args.num_classes
                << " registered classes>";
    });
}

void Vst3Logger::log_response(bool is_host_vst, const Configuration&) {
    log_response_base(is_host_vst,
                      [&](auto& message) { message << "<Configuration>"; });
}

void Vst3Logger::log_response(
    bool is_host_vst,
    const YaProgramListData::GetProgramDataResponse& response) {
    log_response_base(is_host_vst, [&](auto& message) {
        message << response.result.string();
        if (response.result == Steinberg::kResultOk) {
            message << ", <IBStream* containing " << response.data.size()
                    << " bytes>";
        }
    });
}

void Vst3Logger::log_response(bool is_host_vst,
                              const YaUnitData::GetUnitDataResponse& response) {
    log_response_base(is_host_vst, [&](auto& message) {
        message << response.result.string();
        if (response.result == Steinberg::kResultOk) {
            message << ", <IBStream* containing " << response.data.size()
                    << " bytes>";
        }
    });
}

void Vst3Logger::log_response(bool is_host_vst,
                              const YaUnitInfo::GetUnitInfoResponse& response) {
    log_response_base(is_host_vst, [&](auto& message) {
        message << response.result.string();
        if (response.result == Steinberg::kResultOk) {
            message << ", <UnitInfo for \""
                    << VST3::StringConvert::convert(response.info.name)
                    << "\">";
        }
    });
}

void Vst3Logger::log_response(
    bool is_host_vst,
    const YaUnitInfo::GetProgramListInfoResponse& response) {
    log_response_base(is_host_vst, [&](auto& message) {
        message << response.result.string();
        if (response.result == Steinberg::kResultOk) {
            message << ", <ProgramListInfo for \""
                    << VST3::StringConvert::convert(response.info.name)
                    << "\">";
        }
    });
}

void Vst3Logger::log_response(
    bool is_host_vst,
    const YaUnitInfo::GetProgramNameResponse& response) {
    log_response_base(is_host_vst, [&](auto& message) {
        message << response.result.string();
        if (response.result == Steinberg::kResultOk) {
            message << ", \"" << VST3::StringConvert::convert(response.name)
                    << "\"";
        }
    });
}

void Vst3Logger::log_response(
    bool is_host_vst,
    const YaUnitInfo::GetProgramInfoResponse& response) {
    log_response_base(is_host_vst, [&](auto& message) {
        message << response.result.string();
        if (response.result == Steinberg::kResultOk) {
            message << ", \""
                    << VST3::StringConvert::convert(response.attribute_value)
                    << "\"";
        }
    });
}

void Vst3Logger::log_response(
    bool is_host_vst,
    const YaUnitInfo::GetProgramPitchNameResponse& response) {
    log_response_base(is_host_vst, [&](auto& message) {
        message << response.result.string();
        if (response.result == Steinberg::kResultOk) {
            message << ", \"" << VST3::StringConvert::convert(response.name)
                    << "\"";
        }
    });
}

void Vst3Logger::log_response(
    bool is_host_vst,
    const YaUnitInfo::GetUnitByBusResponse& response) {
    log_response_base(is_host_vst, [&](auto& message) {
        message << response.result.string();
        if (response.result == Steinberg::kResultOk) {
            message << ", unit #" << response.unit_id;
        }
    });
}

void Vst3Logger::log_response(
    bool is_host_vst,
    const YaAudioProcessor::GetBusArrangementResponse& response) {
    log_response_base(is_host_vst, [&](auto& message) {
        message << response.result.string();
        if (response.result == Steinberg::kResultOk) {
            message << ", <SpeakerArrangement: 0b"
                    << std::bitset<sizeof(Steinberg::Vst::SpeakerArrangement)>(
                           response.updated_arr)
                    << ">";
        }
    });
}

void Vst3Logger::log_response(
    bool is_host_vst,
    const YaAudioProcessor::ProcessResponse& response) {
    log_response_base(is_host_vst, [&](auto& message) {
        message << response.result.string();

        // This is incredibly verbose, but if you're really a plugin that
        // handles processing in a weird way you're going to need all of this

        std::ostringstream num_output_channels;
        num_output_channels << "[";
        for (bool is_first = true;
             const auto& buffers : response.output_data.outputs) {
            num_output_channels << (is_first ? "" : ", ")
                                << buffers.num_channels();
            is_first = false;
        }
        num_output_channels << "]";

        message << ", <AudioBusBuffers array with " << num_output_channels.str()
                << " channels>";

        if (response.output_data.output_parameter_changes) {
            message << ", <IParameterChanges* for "
                    << response.output_data.output_parameter_changes
                           ->num_parameters()
                    << " parameters>";
        } else {
            message << ", host does not support parameter outputs";
        }

        if (response.output_data.output_events) {
            message << ", <IEventList* with "
                    << response.output_data.output_events->num_events()
                    << " events>";
        } else {
            message << ", host does not support event outputs";
        }
    });
}

void Vst3Logger::log_response(
    bool is_host_vst,
    const YaComponent::GetControllerClassIdResponse& response) {
    log_response_base(is_host_vst, [&](auto& message) {
        message << response.result.string();
        if (response.result == Steinberg::kResultOk) {
            message << ", "
                    << format_uid(Steinberg::FUID::fromTUID(
                           response.editor_cid.data()));
        }
    });
}

void Vst3Logger::log_response(bool is_host_vst,
                              const YaComponent::GetBusInfoResponse& response) {
    log_response_base(is_host_vst, [&](auto& message) {
        message << response.result.string();
        if (response.result == Steinberg::kResultOk) {
            message << ", <BusInfo for \""
                    << VST3::StringConvert::convert(response.updated_bus.name)
                    << "\" with " << response.updated_bus.channelCount
                    << " channels, type = " << response.updated_bus.busType
                    << ", flags = " << response.updated_bus.flags << ">";
        }
    });
}

void Vst3Logger::log_response(
    bool is_host_vst,
    const YaComponent::GetRoutingInfoResponse& response) {
    log_response_base(is_host_vst, [&](auto& message) {
        message << response.result.string();
        if (response.result == Steinberg::kResultOk) {
            message << ", <RoutingInfo& for bus "
                    << response.updated_in_info.busIndex << " and channel "
                    << response.updated_in_info.channel
                    << ", <RoutingInfo& for bus "
                    << response.updated_out_info.busIndex << " and channel "
                    << response.updated_out_info.channel << ">";
        }
    });
}

void Vst3Logger::log_response(
    bool is_host_vst,
    const YaHostApplication::GetNameResponse& response) {
    log_response_base(is_host_vst, [&](auto& message) {
        message << response.result.string();
        if (response.result == Steinberg::kResultOk) {
            std::string value = VST3::StringConvert::convert(response.name);
            message << ", \"" << value << "\"";
        }
    });
}
