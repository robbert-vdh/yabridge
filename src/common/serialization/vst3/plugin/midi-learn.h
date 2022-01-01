// yabridge: a Wine VST bridge
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

#include <pluginterfaces/vst/ivstmidilearn.h>

#include "../../common.h"
#include "../base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IMidiLearn` for serialization purposes. This is instantiated as
 * part of `Vst3PluginProxy`.
 */
class YaMidiLearn : public Steinberg::Vst::IMidiLearn {
   public:
    /**
     * These are the arguments for creating a `YaMidiLearn`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements `IMidiLearn`
         * and read arguments from it.
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
    YaMidiLearn(ConstructArgs&& args) noexcept;

    inline bool supported() const noexcept { return arguments.supported; }

    /**
     * Message to pass through a call to
     * `IMidiLearn::onLiveMIDIControllerInput(bus_index, channel, midi_cc)` to
     * the Wine plugin host.
     */
    struct OnLiveMIDIControllerInput {
        using Response = UniversalTResult;

        native_size_t instance_id;

        int32 bus_index;
        int16 channel;
        Steinberg::Vst::CtrlNumber midi_cc;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(bus_index);
            s.value2b(channel);
            s.value2b(midi_cc);
        }
    };

    virtual tresult PLUGIN_API
    onLiveMIDIControllerInput(int32 busIndex,
                              int16 channel,
                              Steinberg::Vst::CtrlNumber midiCC) override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop
