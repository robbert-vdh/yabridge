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
 * Wraps around `INoteExpressionController` for serialization purposes. This is
 * instantiated as part of `Vst3PluginProxy`.
 */
class YaNoteExpressionController
    : public Steinberg::Vst::INoteExpressionController {
   public:
    /**
     * These are the arguments for creating a `YaNoteExpressionController`.
     */
    struct ConstructArgs {
        ConstructArgs();

        /**
         * Check whether an existing implementation implements
         * `INoteExpressionController` and read arguments from it.
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
    YaNoteExpressionController(const ConstructArgs&& args);

    inline bool supported() const { return arguments.supported; }

    virtual int32 PLUGIN_API getNoteExpressionCount(int32 busIndex,
                                                    int16 channel) override = 0;
    virtual tresult PLUGIN_API getNoteExpressionInfo(
        int32 busIndex,
        int16 channel,
        int32 noteExpressionIndex,
        Steinberg::Vst::NoteExpressionTypeInfo& info /*out*/) override = 0;
    virtual tresult PLUGIN_API getNoteExpressionStringByValue(
        int32 busIndex,
        int16 channel,
        Steinberg::Vst::NoteExpressionTypeID id,
        Steinberg::Vst::NoteExpressionValue valueNormalized /*in*/,
        Steinberg::Vst::String128 string /*out*/) override = 0;
    virtual tresult PLUGIN_API getNoteExpressionValueByString(
        int32 busIndex,
        int16 channel,
        Steinberg::Vst::NoteExpressionTypeID id,
        const Steinberg::Vst::TChar* string /*in*/,
        Steinberg::Vst::NoteExpressionValue& valueNormalized /*out*/)
        override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop
