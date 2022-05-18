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

#include <pluginterfaces/vst/ivstplugview.h>

#include "../../common.h"
#include "../base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IParameterFinder` for serialization purposes. This is
 * instantiated as part of `Vst3PlugViewProxy`.
 */
class YaParameterFinder : public Steinberg::Vst::IParameterFinder {
   public:
    /**
     * These are the arguments for creating a `YaParameterFinder`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements
         * `IParameterFinder` and read arguments from it.
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
    YaParameterFinder(ConstructArgs&& args) noexcept;

    virtual ~YaParameterFinder() noexcept = default;

    inline bool supported() const noexcept { return arguments_.supported; }

    /**
     * The response code and editor size returned by a call to
     * `IParameterFinder::findParameter(x_pos, y_pos, &result_tag)`.
     */
    struct FindParameterResponse {
        UniversalTResult result;
        Steinberg::Vst::ParamID result_tag;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.value4b(result_tag);
        }
    };

    /**
     * Message to pass through a call to `IParameterFinder::findParameter(x_pos,
     * y_pos, &result_tag)` to the Wine plugin host.
     */
    struct FindParameter {
        using Response = FindParameterResponse;

        native_size_t owner_instance_id;

        int32 x_pos;
        int32 y_pos;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value4b(x_pos);
            s.value4b(y_pos);
        }
    };

    virtual tresult PLUGIN_API
    findParameter(int32 xPos,
                  int32 yPos,
                  Steinberg::Vst::ParamID& resultTag /*out*/) override = 0;

   protected:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop
