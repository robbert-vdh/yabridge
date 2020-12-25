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

#include <pluginterfaces/vst/ivsteditcontroller.h>

#include "../../common.h"
#include "../base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IEditController2` for serialization purposes. This is
 * instantiated as part of `Vst3PluginProxy`.
 */
class YaEditController2 : public Steinberg::Vst::IEditController2 {
   public:
    /**
     * These are the arguments for creating a `YaEditController2`.
     */
    struct ConstructArgs {
        ConstructArgs();

        /**
         * Check whether an existing implementation implements
         * `IEditController2` and read arguments from it.
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
    YaEditController2(const ConstructArgs&& args);

    inline bool supported() const { return arguments.supported; }

    /**
     * Message to pass through a call to `IEditController2::setKnobMode(mode)`
     * to the Wine plugin host.
     */
    struct SetKnobMode {
        using Response = UniversalTResult;

        native_size_t instance_id;

        Steinberg::Vst::KnobMode mode;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(mode);
        }
    };

    virtual tresult PLUGIN_API
    setKnobMode(Steinberg::Vst::KnobMode mode) override = 0;
    virtual tresult PLUGIN_API openHelp(TBool onlyCheck) override = 0;
    virtual tresult PLUGIN_API openAboutBox(TBool onlyCheck) override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop
