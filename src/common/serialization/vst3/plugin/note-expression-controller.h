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

#include <pluginterfaces/vst/ivstnoteexpression.h>

#include "../../common.h"
#include "../base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `INoteExpressionController` for serialization purposes. This is
 * instantiated as part of `Vst3PluginProxy`.
 *
 * TODO: Check if these things are being called in the processing loop. If they
 *       are, handle them on the audio sockets.
 */
class YaNoteExpressionController
    : public Steinberg::Vst::INoteExpressionController {
   public:
    /**
     * These are the arguments for creating a `YaNoteExpressionController`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements
         * `INoteExpressionController` and read arguments from it.
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
    YaNoteExpressionController(ConstructArgs&& args) noexcept;

    virtual ~YaNoteExpressionController() noexcept = default;

    inline bool supported() const noexcept { return arguments_.supported; }

    /**
     * Message to pass through a call to
     * `INoteExpressionController::getNoteExpressionCount(bus_index, channel)`
     * to the Wine plugin host.
     */
    struct GetNoteExpressionCount {
        using Response = PrimitiveResponse<int32>;

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

    virtual int32 PLUGIN_API getNoteExpressionCount(int32 busIndex,
                                                    int16 channel) override = 0;

    /**
     * The response code and returned info for a call to
     * `INoteExpressionController::getNoteExpressionInfo(bus_index, channel,
     * note_expression_index, &info)`.
     */
    struct GetNoteExpressionInfoResponse {
        UniversalTResult result;
        Steinberg::Vst::NoteExpressionTypeInfo info;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.object(info);
        }
    };

    /**
     * Message to pass through a call to
     * `INoteExpressionController::getNoteExpressionInfo(bus_index, channel,
     * note_expression_index, &info)` to the Wine plugin host.
     */
    struct GetNoteExpressionInfo {
        using Response = GetNoteExpressionInfoResponse;

        native_size_t instance_id;

        int32 bus_index;
        int16 channel;
        int32 note_expression_index;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(bus_index);
            s.value2b(channel);
            s.value4b(note_expression_index);
        }
    };

    virtual tresult PLUGIN_API getNoteExpressionInfo(
        int32 busIndex,
        int16 channel,
        int32 noteExpressionIndex,
        Steinberg::Vst::NoteExpressionTypeInfo& info /*out*/) override = 0;

    /**
     * The response code and returned string for a call to
     * `INoteExpressionController::getNoteExpressionStringByValue(bus_index,
     * channel, id, value_normalized, &string)`.
     */
    struct GetNoteExpressionStringByValueResponse {
        UniversalTResult result;
        std::u16string string;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.text2b(string, std::extent_v<Steinberg::Vst::String128>);
        }
    };

    /**
     * Message to pass through a call to
     * `INoteExpressionController::getNoteExpressionStringByValue(bus_index,
     * channel, id, value_normalized, &string)` to the Wine plugin host.
     */
    struct GetNoteExpressionStringByValue {
        using Response = GetNoteExpressionStringByValueResponse;

        native_size_t instance_id;

        int32 bus_index;
        int16 channel;
        Steinberg::Vst::NoteExpressionTypeID id;
        Steinberg::Vst::NoteExpressionValue value_normalized;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(bus_index);
            s.value2b(channel);
            s.value4b(id);
            s.value8b(value_normalized);
        }
    };

    virtual tresult PLUGIN_API getNoteExpressionStringByValue(
        int32 busIndex,
        int16 channel,
        Steinberg::Vst::NoteExpressionTypeID id,
        Steinberg::Vst::NoteExpressionValue valueNormalized /*in*/,
        Steinberg::Vst::String128 string /*out*/) override = 0;

    /**
     * The response code and returned value for a call to
     * `INoteExpressionController::getNoteExpressionValueByString(bus_index,
     * channel, id, string, &value_normalized)`.
     */
    struct GetNoteExpressionValueByStringResponse {
        UniversalTResult result;
        Steinberg::Vst::NoteExpressionValue value_normalized;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.value8b(value_normalized);
        }
    };

    /**
     * Message to pass through a call to
     * `INoteExpressionController::getNoteExpressionValueByString(bus_index,
     * channel, id, string, &value_normalized)` to the Wine plugin host.
     */
    struct GetNoteExpressionValueByString {
        using Response = GetNoteExpressionValueByStringResponse;

        native_size_t instance_id;

        int32 bus_index;
        int16 channel;
        Steinberg::Vst::NoteExpressionTypeID id;
        std::u16string string;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(bus_index);
            s.value2b(channel);
            s.value4b(id);
            s.text2b(string, std::extent_v<Steinberg::Vst::String128>);
        }
    };

    virtual tresult PLUGIN_API getNoteExpressionValueByString(
        int32 busIndex,
        int16 channel,
        Steinberg::Vst::NoteExpressionTypeID id,
        const Steinberg::Vst::TChar* string /*in*/,
        Steinberg::Vst::NoteExpressionValue& valueNormalized /*out*/)
        override = 0;

   protected:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop

namespace Steinberg {
namespace Vst {
template <typename S>
void serialize(S& s, NoteExpressionTypeInfo& info) {
    s.value4b(info.typeId);
    s.container2b(info.title);
    s.container2b(info.shortTitle);
    s.container2b(info.units);
    s.value4b(info.unitId);
    s.object(info.valueDesc);
    s.value4b(info.associatedParameterId);
    s.value4b(info.flags);
}

template <typename S>
void serialize(S& s, NoteExpressionValueDescription& description) {
    s.value8b(description.defaultValue);
    s.value8b(description.minimum);
    s.value8b(description.maximum);
    s.value4b(description.stepCount);
}
}  // namespace Vst
}  // namespace Steinberg
