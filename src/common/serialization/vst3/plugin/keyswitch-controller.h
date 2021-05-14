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

#include <pluginterfaces/vst/ivstnoteexpression.h>

#include "../../common.h"
#include "../base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IKeyswitchController` for serialization purposes. This is
 * instantiated as part of `Vst3PluginProxy`.
 */
class YaKeyswitchController : public Steinberg::Vst::IKeyswitchController {
   public:
    /**
     * These are the arguments for creating a `YaKeyswitchController`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements
         * `IKeyswitchController` and read arguments from it.
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
    YaKeyswitchController(const ConstructArgs&& args) noexcept;

    inline bool supported() const noexcept { return arguments.supported; }

    /**
     * Message to pass through a call to
     * `IKeyswitchController::getKeyswitchCount(bus_index, channel)` to the Wine
     * plugin host.
     */
    struct GetKeyswitchCount {
        using Response = PrimitiveWrapper<int32>;

        native_size_t instance_id;

        int32 bus_index;
        int16 channel;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(bus_index);
            s.value2b(channel);
        }
    };

    virtual int32 PLUGIN_API getKeyswitchCount(int32 busIndex,
                                               int16 channel) override = 0;

    /**
     * The response code and written state for a call to
     * `IKeyswitchController::getKeyswitchInfo(bus_index, channel,
     * key_switch_index, &info)`.
     */
    struct GetKeyswitchInfoResponse {
        UniversalTResult result;
        Steinberg::Vst::KeyswitchInfo info;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.object(info);
        }
    };

    /**
     * Message to pass through a call to
     * `IKeyswitchController::getKeyswitchInfo(bus_index, channel,
     * key_switch_index, &info)` to the Wine plugin host.
     */
    struct GetKeyswitchInfo {
        using Response = GetKeyswitchInfoResponse;

        native_size_t instance_id;

        int32 bus_index;
        int16 channel;
        int32 key_switch_index;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(bus_index);
            s.value2b(channel);
            s.value4b(key_switch_index);
        }
    };

    virtual tresult PLUGIN_API
    getKeyswitchInfo(int32 busIndex,
                     int16 channel,
                     int32 keySwitchIndex,
                     Steinberg::Vst::KeyswitchInfo& info /*out*/) override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop

namespace Steinberg {
namespace Vst {
template <typename S>
void serialize(S& s, Steinberg::Vst::KeyswitchInfo& info) {
    s.value4b(info.typeId);
    s.container2b(info.title);
    s.container2b(info.shortTitle);
    s.value4b(info.keyswitchMin);
    s.value4b(info.keyswitchMax);
    s.value4b(info.keyRemapped);
    s.value4b(info.unitId);
    s.value4b(info.flags);
}
}  // namespace Vst
}  // namespace Steinberg
