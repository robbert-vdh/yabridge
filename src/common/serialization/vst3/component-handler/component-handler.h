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

#include <pluginterfaces/vst/ivsteditcontroller.h>

#include "../../common.h"
#include "../base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IComponentHandler` for serialization purposes. This is
 * instantiated as part of `Vst3ComponentHandlerProxy`.
 */
class YaComponentHandler : public Steinberg::Vst::IComponentHandler {
   public:
    /**
     * These are the arguments for creating a `YaComponentHandler`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements
         * `IComponentHandler` and read arguments from it.
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
    YaComponentHandler(ConstructArgs&& args) noexcept;

    inline bool supported() const noexcept { return arguments.supported; }

    /**
     * Message to pass through a call to `IComponentHandler::beginEdit(id)` to
     * the component handler provided by the host.
     */
    struct BeginEdit {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        Steinberg::Vst::ParamID id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value4b(id);
        }
    };

    virtual tresult PLUGIN_API
    beginEdit(Steinberg::Vst::ParamID id) override = 0;

    /**
     * Message to pass through a call to `IComponentHandler::performEdit(id,
     * value_normalized)` to the component handler provided by the host.
     */
    struct PerformEdit {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        Steinberg::Vst::ParamID id;
        Steinberg::Vst::ParamValue value_normalized;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value4b(id);
            s.value8b(value_normalized);
        }
    };

    virtual tresult PLUGIN_API
    performEdit(Steinberg::Vst::ParamID id,
                Steinberg::Vst::ParamValue valueNormalized) override = 0;

    /**
     * Message to pass through a call to `IComponentHandler::endEdit(id)` to the
     * component handler provided by the host.
     */
    struct EndEdit {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        Steinberg::Vst::ParamID id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value4b(id);
        }
    };

    virtual tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override = 0;

    /**
     * Message to pass through a call to
     * `IComponentHandler::restartComponent(flags)` to the component handler
     * provided by the host.
     */
    struct RestartComponent {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        int32 flags;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value4b(flags);
        }
    };

    virtual tresult PLUGIN_API restartComponent(int32 flags) override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop
