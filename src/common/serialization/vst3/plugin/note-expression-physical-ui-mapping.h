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

#include <pluginterfaces/vst/ivstphysicalui.h>

#include "../../common.h"
#include "../base.h"
#include "../physical-ui-map-list.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `INoteExpressionPhysicalUIMapping` for serialization purposes.
 * This is instantiated as part of `Vst3PluginProxy`.
 */
class YaNoteExpressionPhysicalUIMapping
    : public Steinberg::Vst::INoteExpressionPhysicalUIMapping {
   public:
    /**
     * These are the arguments for creating a
     * `YaNoteExpressionPhysicalUIMapping`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements
         * `INoteExpressionPhysicalUIMapping` and read arguments from it.
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
    YaNoteExpressionPhysicalUIMapping(ConstructArgs&& args) noexcept;

    inline bool supported() const noexcept { return arguments.supported; }

    /**
     * The response code and returned info for a call to
     * `INoteExpressionPhysicalUIMapping::getNotePhysicalUIMapping(bus_index,
     * channel, list)`.
     */
    struct GetNotePhysicalUIMappingResponse {
        UniversalTResult result;
        /**
         * The list as updated by the plugin.
         */
        YaPhysicalUIMapList list;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.object(list);
        }
    };

    /**
     * Message to pass through a call to
     * `INoteExpressionPhysicalUIMapping::getNotePhysicalUIMapping(bus_index,
     * channel, list)` to the Wine plugin host.
     */
    struct GetNotePhysicalUIMapping {
        using Response = GetNotePhysicalUIMappingResponse;

        native_size_t instance_id;

        int32 bus_index;
        int16 channel;
        /**
         * The host will provide a partially filled of physical controls, and
         * the plugin has to assign note expression IDs to each of them.
         */
        YaPhysicalUIMapList list;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(bus_index);
            s.value2b(channel);
        }
    };

    virtual tresult PLUGIN_API
    getPhysicalUIMapping(int32 busIndex,
                         int16 channel,
                         Steinberg::Vst::PhysicalUIMapList& list) override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop
