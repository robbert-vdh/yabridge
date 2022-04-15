// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2022 Robbert van der Helm
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

#include <pluginterfaces/vst/ivstunits.h>

#include "../../common.h"
#include "../base.h"
#include "../bstream.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IUnitInfo` for serialization purposes. This is instantiated as
 * part of `Vst3PluginProxy`.
 */
class YaUnitInfo : public Steinberg::Vst::IUnitInfo {
   public:
    /**
     * These are the arguments for creating a `YaUnitInfo`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements `IUnitInfo` and
         * read arguments from it.
         */
        ConstructArgs(Steinberg::IPtr<Steinberg::FUnknown> object) noexcept;

        /**
         * Whether the object supported this interface.
         */
        bool supported;

        template <typename S>
        void serialize(S& s) {
            s.value1b(supported);
        }
    };

    /**
     * Instantiate this instance with arguments read from another interface
     * implementation.
     */
    YaUnitInfo(ConstructArgs&& args) noexcept;

    inline bool supported() const noexcept { return arguments_.supported; }

    /**
     * Message to pass through a call to `IUnitInfo::getUnitCount()` to the Wine
     * plugin host.
     */
    struct GetUnitCount {
        using Response = PrimitiveWrapper<int32>;

        native_size_t instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
        }
    };

    virtual int32 PLUGIN_API getUnitCount() override = 0;

    /**
     * The response code and returned unit information for a call to
     * `IUnitInfo::getUnitInfo(unit_index, &info)`.
     */
    struct GetUnitInfoResponse {
        UniversalTResult result;
        Steinberg::Vst::UnitInfo info;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.object(info);
        }
    };

    /**
     * Message to pass through a call to `IUnitInfo::getUnitInfo(unit_index,
     * &info)` to the Wine plugin host.
     */
    struct GetUnitInfo {
        using Response = GetUnitInfoResponse;

        native_size_t instance_id;

        int32 unit_index;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(unit_index);
        }
    };

    virtual tresult PLUGIN_API
    getUnitInfo(int32 unitIndex,
                Steinberg::Vst::UnitInfo& info /*out*/) override = 0;

    /**
     * Message to pass through a call to `IUnitInfo::getProgramListCount()` to
     * the Wine plugin host.
     */
    struct GetProgramListCount {
        using Response = PrimitiveWrapper<int32>;

        native_size_t instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
        }
    };

    virtual int32 PLUGIN_API getProgramListCount() override = 0;

    /**
     * The response code and returned unit information for a call to
     * `IUnitInfo::getProgramListInfo(list_index, &info)`.
     */
    struct GetProgramListInfoResponse {
        UniversalTResult result;
        Steinberg::Vst::ProgramListInfo info;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.object(info);
        }
    };

    /**
     * Message to pass through a call to
     * `IUnitInfo::getProgramListInfo(list_index, &info)` to the Wine plugin
     * host.
     */
    struct GetProgramListInfo {
        using Response = GetProgramListInfoResponse;

        native_size_t instance_id;

        int32 list_index;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(list_index);
        }
    };

    virtual tresult PLUGIN_API getProgramListInfo(
        int32 listIndex,
        Steinberg::Vst::ProgramListInfo& info /*out*/) override = 0;

    /**
     * The response code and returned name for a call to
     * `IUnitInfo::getProgramName(list_id, program_index, &name)`.
     */
    struct GetProgramNameResponse {
        UniversalTResult result;
        std::u16string name;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.text2b(name, std::extent_v<Steinberg::Vst::String128>);
        }
    };

    /**
     * Message to pass through a call to `IUnitInfo::getProgramName(list_id,
     * program_index, &name)` to the Wine plugin host.
     */
    struct GetProgramName {
        using Response = GetProgramNameResponse;

        native_size_t instance_id;

        Steinberg::Vst::ProgramListID list_id;
        int32 program_index;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(list_id);
            s.value4b(program_index);
        }
    };

    virtual tresult PLUGIN_API
    getProgramName(Steinberg::Vst::ProgramListID listId,
                   int32 programIndex,
                   Steinberg::Vst::String128 name /*out*/) override = 0;

    /**
     * The response code and returned value for a call to
     * `IUnitInfo::getPrograminfo(list_id, program_index, attribute_name,
     * &attribute_value)`.
     */
    struct GetProgramInfoResponse {
        UniversalTResult result;
        std::u16string attribute_value;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.text2b(attribute_value, std::extent_v<Steinberg::Vst::String128>);
        }
    };

    /**
     * Message to pass through a call to `IUnitInfo::getProgramInfo(list_id,
     * program_index, attribute_id, &attribute_value)` to the Wine plugin host.
     */
    struct GetProgramInfo {
        using Response = GetProgramInfoResponse;

        native_size_t instance_id;

        Steinberg::Vst::ProgramListID list_id;
        int32 program_index;
        std::string attribute_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(list_id);
            s.value4b(program_index);
            s.text1b(attribute_id, 256);
        }
    };

    virtual tresult PLUGIN_API getProgramInfo(
        Steinberg::Vst::ProgramListID listId,
        int32 programIndex,
        Steinberg::Vst::CString attributeId /*in*/,
        Steinberg::Vst::String128 attributeValue /*out*/) override = 0;

    /**
     * Message to pass through a call to
     * `IUnitInfo::hasProgramPitchNames(list_id, program_index)` to the Wine
     * plugin host.
     */
    struct HasProgramPitchNames {
        using Response = UniversalTResult;

        native_size_t instance_id;

        Steinberg::Vst::ProgramListID list_id;
        int32 program_index;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(list_id);
            s.value4b(program_index);
        }
    };

    virtual tresult PLUGIN_API
    hasProgramPitchNames(Steinberg::Vst::ProgramListID listId,
                         int32 programIndex) override = 0;

    /**
     * The response code and returned name for a call to
     * `IUnitInfo::getProgramPitchName(list_id, program_index, midi_pitch,
     * &name)`.
     */
    struct GetProgramPitchNameResponse {
        UniversalTResult result;
        std::u16string name;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.text2b(name, std::extent_v<Steinberg::Vst::String128>);
        }
    };

    /**
     * Message to pass through a call to
     * `IUnitInfo::getProgramPitchName(list_id, program_index, midi_pitch,
     * &name)` to the Wine plugin host.
     */
    struct GetProgramPitchName {
        using Response = GetProgramPitchNameResponse;

        native_size_t instance_id;

        Steinberg::Vst::ProgramListID list_id;
        int32 program_index;
        int16 midi_pitch;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(list_id);
            s.value4b(program_index);
            s.value2b(midi_pitch);
        }
    };

    virtual tresult PLUGIN_API
    getProgramPitchName(Steinberg::Vst::ProgramListID listId,
                        int32 programIndex,
                        int16 midiPitch,
                        Steinberg::Vst::String128 name /*out*/) override = 0;

    /**
     * Message to pass through a call to `IUnitInfo::getSelectedUnit()` to the
     * Wine plugin host.
     */
    struct GetSelectedUnit {
        using Response = PrimitiveWrapper<Steinberg::Vst::UnitID>;

        native_size_t instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
        }
    };

    virtual Steinberg::Vst::UnitID PLUGIN_API getSelectedUnit() override = 0;

    /**
     * Message to pass through a call to `IUnitInfo::selectUnit(unit_id)` to the
     * Wine plugin host.
     */
    struct SelectUnit {
        using Response = UniversalTResult;

        native_size_t instance_id;

        Steinberg::Vst::UnitID unit_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(unit_id);
        }
    };

    virtual tresult PLUGIN_API
    selectUnit(Steinberg::Vst::UnitID unitId) override = 0;

    /**
     * The response code and returned unit ID for a call to
     * `IUnitInfo::getUnitByBus(type, dir, bus_index, channel, &unit_id)`.
     */
    struct GetUnitByBusResponse {
        UniversalTResult result;
        Steinberg::Vst::UnitID unit_id;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.value4b(unit_id);
        }
    };

    /**
     * Message to pass through a call to `IUnitInfo::getUnitByBus(type, dir,
     * bus_index, channel, &unit_id)` to the Wine plugin host.
     */
    struct GetUnitByBus {
        using Response = GetUnitByBusResponse;

        native_size_t instance_id;

        Steinberg::Vst::MediaType type;
        Steinberg::Vst::BusDirection dir;
        int32 bus_index;
        int32 channel;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(type);
            s.value4b(dir);
            s.value4b(bus_index);
            s.value4b(channel);
        }
    };

    virtual tresult PLUGIN_API
    getUnitByBus(Steinberg::Vst::MediaType type,
                 Steinberg::Vst::BusDirection dir,
                 int32 busIndex,
                 int32 channel,
                 Steinberg::Vst::UnitID& unitId /*out*/) override = 0;

    /*
     * Message to pass through a call to
     * `IUnitInfo::setUnitProgramData(list_or_unit_id, program_index, data)` to
     * the Wine plugin host.
     */
    struct SetUnitProgramData {
        using Response = UniversalTResult;

        native_size_t instance_id;

        int32 list_or_unit_id;
        int32 program_index;
        YaBStream data;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(list_or_unit_id);
            s.value4b(program_index);
            s.object(data);
        }
    };

    virtual tresult PLUGIN_API
    setUnitProgramData(int32 listOrUnitId,
                       int32 programIndex,
                       Steinberg::IBStream* data) override = 0;

   protected:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop

namespace Steinberg {
namespace Vst {
template <typename S>
void serialize(S& s, UnitInfo& info) {
    s.value4b(info.id);
    s.value4b(info.parentUnitId);
    s.container2b(info.name);
    s.value4b(info.programListId);
}

template <typename S>
void serialize(S& s, ProgramListInfo& info) {
    s.value4b(info.id);
    s.container2b(info.name);
    s.value4b(info.programCount);
}
}  // namespace Vst
}  // namespace Steinberg
