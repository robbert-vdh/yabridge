// yabridge: a Wine VST bridge
// Copyright (C) 2020-2021 Robbert van der Helm
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
 * Wraps around `IProgramListData` for serialization purposes. This is
 * instantiated as part of `Vst3PluginProxy`.
 */
class YaProgramListData : public Steinberg::Vst::IProgramListData {
   public:
    /**
     * These are the arguments for creating a `YaProgramListData`.
     */
    struct ConstructArgs {
        ConstructArgs();

        /**
         * Check whether an existing implementation implements
         * `IProgramListData` and read arguments from it.
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
    YaProgramListData(const ConstructArgs&& args);

    inline bool supported() const { return arguments.supported; }

    /**
     * Message to pass through a call to
     * `IProgramListData::programDataSupported(list_id)` to the Wine plugin
     * host.
     */
    struct ProgramDataSupported {
        using Response = UniversalTResult;

        native_size_t instance_id;

        Steinberg::Vst::ProgramListID list_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(list_id);
        }
    };

    virtual tresult PLUGIN_API
    programDataSupported(Steinberg::Vst::ProgramListID listId) override = 0;

    /**
     * The response code and written state for a call to
     * `IProgramListData::getProgramData(list_id, program_index, &data)`.
     */
    struct GetProgramDataResponse {
        UniversalTResult result;
        YaBStream data;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.object(data);
        }
    };

    /**
     * Message to pass through a call to
     * `IProgramListData::getProgramData(list_id, program_index, &data)` to the
     * Wine plugin host.
     */
    struct GetProgramData {
        using Response = GetProgramDataResponse;

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
    getProgramData(Steinberg::Vst::ProgramListID listId,
                   int32 programIndex,
                   Steinberg::IBStream* data) override = 0;

    /**
     * Message to pass through a call to
     * `IProgramListData::SetProgramData(list_id, program_index, data)` to the
     * Wine plugin host.
     */
    struct SetProgramData {
        using Response = UniversalTResult;

        native_size_t instance_id;

        Steinberg::Vst::ProgramListID list_id;
        int32 program_index;
        YaBStream data;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(list_id);
            s.value4b(program_index);
            s.object(data);
        }
    };

    virtual tresult PLUGIN_API
    setProgramData(Steinberg::Vst::ProgramListID listId,
                   int32 programIndex,
                   Steinberg::IBStream* data) override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop
