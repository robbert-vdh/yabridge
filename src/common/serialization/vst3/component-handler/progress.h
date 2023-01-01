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

#include <pluginterfaces/vst/ivsteditcontroller.h>

#include "../../../bitsery/ext/in-place-optional.h"
#include "../../common.h"
#include "../base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IProgress` for serialization purposes. This is instantiated as
 * part of `Vst3ComponentHandlerProxy`.
 */
class YaProgress : public Steinberg::Vst::IProgress {
   public:
    /**
     * These are the arguments for creating a `YaProgress`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements `IProgress`
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
    YaProgress(ConstructArgs&& args) noexcept;

    virtual ~YaProgress() noexcept = default;

    inline bool supported() const noexcept { return arguments_.supported; }

    /**
     * The response code and returned ID for a call to `IProgress::start(type,
     * optional_description, &out_id)`.
     */
    struct StartResponse {
        UniversalTResult result;
        ID out_id;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.value8b(out_id);
        }
    };

    /**
     * Message to pass through a call to `IProgress::start(type,
     * optional_description, &out_id)` to the component handler provided by the
     * host.
     */
    struct Start {
        using Response = StartResponse;

        native_size_t owner_instance_id;

        ProgressType type;
        /**
         * The docs mention that this is optional. They don't specify whether
         * optional means a null pointer or an empty string.
         */
        std::optional<std::u16string> optional_description;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value4b(type);
            s.ext(optional_description, bitsery::ext::InPlaceOptional{},
                  [](S& s, std::u16string& description) {
                      s.text2b(description, 1024);
                  });
        }
    };

    virtual tresult PLUGIN_API
    start(ProgressType type,
          const Steinberg::tchar* optionalDescription,
          ID& outID) override = 0;

    /**
     * Message to pass through a call to `IProgress::update(id, norm_value)` to
     * the component handler provided by the host.
     */
    struct Update {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        ID id;
        Steinberg::Vst::ParamValue norm_value;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value8b(id);
            s.value8b(norm_value);
        }
    };

    virtual tresult PLUGIN_API
    update(ID id, Steinberg::Vst::ParamValue normValue) override = 0;

    /**
     * Message to pass through a call to `IProgress::finish(id)` to the
     * component handler provided by the host.
     */
    struct Finish {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        ID id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value8b(id);
        }
    };

    virtual tresult PLUGIN_API finish(ID id) override = 0;

   protected:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop
