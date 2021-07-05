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

#include <pluginterfaces/vst/ivsteditcontroller.h>

#include "../../common.h"
#include "../base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IComponentHandler2` for serialization purposes. This is
 * instantiated as part of `Vst3ComponentHandler2Proxy`.
 */
class YaComponentHandler2 : public Steinberg::Vst::IComponentHandler2 {
   public:
    /**
     * These are the arguments for creating a `YaComponentHandler2`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements
         * `IComponentHandler2` and read arguments from it.
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
    YaComponentHandler2(ConstructArgs&& args) noexcept;

    inline bool supported() const noexcept { return arguments.supported; }

    /**
     * Message to pass through a call to `IComponentHandler2::setDirty(state)`
     * to the component handler provided by the host.
     */
    struct SetDirty {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        TBool state;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.value1b(state);
        }
    };

    virtual tresult PLUGIN_API setDirty(TBool state) override = 0;

    /**
     * Message to pass through a call to
     * `IComponentHandler2::requestOpenEditor(name)` to the component handler
     * provided by the host.
     */
    struct RequestOpenEditor {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        std::string name;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.text1b(name, 256);
        }
    };

    virtual tresult PLUGIN_API
    requestOpenEditor(Steinberg::FIDString name) override = 0;

    /**
     * Message to pass through a call to `IComponentHandler2::startGroupEdit()`
     * to the component handler provided by the host.
     */
    struct StartGroupEdit {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
        }
    };

    virtual tresult PLUGIN_API startGroupEdit() override = 0;

    /**
     * Message to pass through a call to `IComponentHandler2::finishGroupEdit()`
     * to the component handler provided by the host.
     */
    struct FinishGroupEdit {
        using Response = UniversalTResult;

        native_size_t owner_instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
        }
    };

    virtual tresult PLUGIN_API finishGroupEdit() override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop
