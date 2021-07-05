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

#include <type_traits>

#include <bitsery/traits/string.h>
#include <pluginterfaces/vst/ivsthostapplication.h>

#include "../../../bitsery/ext/in-place-optional.h"
#include "../../common.h"
#include "../base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IHostApplication` for serialization purposes. This is
 * instantiated as part of `Vst3HostContextProxy`.
 */
class YaHostApplication : public Steinberg::Vst::IHostApplication {
   public:
    /**
     * These are the arguments for creating a `YaHostApplication`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements
         * `IHostApplication` and read arguments from it.
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
    YaHostApplication(ConstructArgs&& args) noexcept;

    inline bool supported() const noexcept { return arguments.supported; }

    /**
     * The response code and resulting value for a call to
     * `IHostApplication::getName()`.
     */
    struct GetNameResponse {
        UniversalTResult result;
        std::u16string name;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.text2b(name, std::extent_v<Steinberg::Vst::String128>);
        }
    };

    /**
     * Message to pass through a call to `IHostApplication::getName()` to the
     * host context provided by the host.
     */
    struct GetName {
        using Response = GetNameResponse;

        /**
         * The object instance whose host context to call this function to. Of
         * empty, then the function will be called on the factory's host context
         * instead.
         */
        std::optional<native_size_t> owner_instance_id;

        template <typename S>
        void serialize(S& s) {
            s.ext(owner_instance_id, bitsery::ext::InPlaceOptional{},
                  [](S& s, native_size_t& instance_id) {
                      s.value8b(instance_id);
                  });
        }
    };

    virtual tresult PLUGIN_API
    getName(Steinberg::Vst::String128 name) override = 0;

    // We don't have to (and can't) pass this through. This is only used to
    // create `IMessage` and `IAttributeList` objects that the plugin can use to
    // pass messages between the processor and the controller objects.
    virtual tresult PLUGIN_API createInstance(Steinberg::TUID cid,
                                              Steinberg::TUID _iid,
                                              void** obj) override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop
