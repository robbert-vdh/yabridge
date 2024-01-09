// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2024 Robbert van der Helm
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

#include <pluginterfaces/vst/ivstpluginterfacesupport.h>

#include "../../../bitsery/ext/in-place-optional.h"
#include "../../common.h"
#include "../base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IPlugInterfaceSupport` for serialization purposes. This is
 * instantiated as part of `Vst3HostContextProxy`.
 */
class YaPlugInterfaceSupport : public Steinberg::Vst::IPlugInterfaceSupport {
   public:
    /**
     * These are the arguments for creating a `YaPlugInterfaceSupport`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements
         * `IPlugInterfaceSupport` and read arguments from it.
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
    YaPlugInterfaceSupport(ConstructArgs&& args) noexcept;

    virtual ~YaPlugInterfaceSupport() noexcept = default;

    inline bool supported() const noexcept { return arguments_.supported; }

    /**
     * Message to pass through a call to
     * `IPlugInterfaceSupport::isPlugInterfaceSupported(iid)` to the host
     * context provided by the host.
     */
    struct IsPlugInterfaceSupported {
        using Response = UniversalTResult;

        /**
         * The object instance whose host context to call this function to. Of
         * empty, then the function will be called on the factory's host context
         * instead.
         */
        std::optional<native_size_t> owner_instance_id;

        WineUID iid;

        template <typename S>
        void serialize(S& s) {
            s.ext(owner_instance_id, bitsery::ext::InPlaceOptional{},
                  [](S& s, native_size_t& instance_id) {
                      s.value8b(instance_id);
                  });
            s.object(iid);
        }
    };

    virtual tresult PLUGIN_API
    isPlugInterfaceSupported(const Steinberg::TUID _iid) override = 0;

   protected:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop
