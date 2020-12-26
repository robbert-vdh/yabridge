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

#pragma once

#include <pluginterfaces/vst/ivstunits.h>

#include "../../common.h"
#include "../base.h"
#include "../host-context-proxy.h"

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
        ConstructArgs();

        /**
         * Check whether an existing implementation implements `IUnitInfo` and
         * read arguments from it.
         */
        ConstructArgs(Steinberg::IPtr<Steinberg::FUnknown> object);

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
    YaUnitInfo(const ConstructArgs&& args);

    inline bool supported() const { return arguments.supported; }

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
     * The response code and returned name for a call to
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
    virtual tresult PLUGIN_API
    hasProgramPitchNames(Steinberg::Vst::ProgramListID listId,
                         int32 programIndex) override = 0;
    virtual tresult PLUGIN_API
    getProgramPitchName(Steinberg::Vst::ProgramListID listId,
                        int32 programIndex,
                        int16 midiPitch,
                        Steinberg::Vst::String128 name /*out*/) override = 0;
    virtual Steinberg::Vst::UnitID PLUGIN_API getSelectedUnit() override = 0;
    virtual tresult PLUGIN_API
    selectUnit(Steinberg::Vst::UnitID unitId) override = 0;
    virtual tresult PLUGIN_API
    getUnitByBus(Steinberg::Vst::MediaType type,
                 Steinberg::Vst::BusDirection dir,
                 int32 busIndex,
                 int32 channel,
                 Steinberg::Vst::UnitID& unitId /*out*/) override = 0;
    virtual tresult PLUGIN_API
    setUnitProgramData(int32 listOrUnitId,
                       int32 programIndex,
                       Steinberg::IBStream* data) override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop

namespace Steinberg {
namespace Vst {
template <typename S>
void serialize(S& s, UnitInfo& info) {
    s.value4b(info.id);
    s.value4b(info.parentUnitId);
    s.text2b(info.name);
    s.value4b(info.programListId);
}

template <typename S>
void serialize(S& s, ProgramListInfo& info) {
    s.value4b(info.id);
    s.text2b(info.name);
    s.value4b(info.programCount);
}
}  // namespace Vst
}  // namespace Steinberg
