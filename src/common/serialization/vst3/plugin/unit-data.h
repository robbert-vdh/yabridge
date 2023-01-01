// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2023 Robbert van der Helm
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
 * Wraps around `IUnitData` for serialization purposes. This is instantiated as
 * part of `Vst3PluginProxy`.
 */
class YaUnitData : public Steinberg::Vst::IUnitData {
   public:
    /**
     * These are the arguments for creating a `YaUnitData`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements `IUnitData` and
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
    YaUnitData(ConstructArgs&& args) noexcept;

    virtual ~YaUnitData() noexcept = default;

    inline bool supported() const noexcept { return arguments_.supported; }

    /**
     * Message to pass through a call to `IUnitData::unitDataSupported(unit_id)`
     * to the Wine plugin host.
     */
    struct UnitDataSupported {
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
    unitDataSupported(Steinberg::Vst::UnitID unitId) override = 0;

    /**
     * The response code and written state for a call to
     * `IUnitData::getUnitData(unit_id, &data)`.
     */
    struct GetUnitDataResponse {
        UniversalTResult result;
        YaBStream data;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.object(data);
        }
    };

    /**
     * Message to pass through a call to `IUnitData::getUnitData(unit_id,
     * &data)` to the Wine plugin host.
     */
    struct GetUnitData {
        using Response = GetUnitDataResponse;

        native_size_t instance_id;

        Steinberg::Vst::UnitID unit_id;
        YaBStream data;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(unit_id);
            s.object(data);
        }
    };

    virtual tresult PLUGIN_API
    getUnitData(Steinberg::Vst::UnitID unitId,
                Steinberg::IBStream* data) override = 0;

    /**
     * Message to pass through a call to `IUnitData::SetUnitData(unit_id, data)`
     * to the Wine plugin host.
     */
    struct SetUnitData {
        using Response = UniversalTResult;

        native_size_t instance_id;

        Steinberg::Vst::UnitID unit_id;
        YaBStream data;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(unit_id);
            s.object(data);
        }
    };

    virtual tresult PLUGIN_API
    setUnitData(Steinberg::Vst::UnitID unitId,
                Steinberg::IBStream* data) override = 0;

   protected:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop
