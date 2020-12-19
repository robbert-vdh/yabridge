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

#include <type_traits>

#include <bitsery/ext/std_optional.h>
#include <bitsery/traits/string.h>
#include <pluginterfaces/vst/ivsthostapplication.h>

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
        ConstructArgs();

        /**
         * Check whether an existing implementation implements
         * `IHostApplication` and read arguments from it.
         */
        ConstructArgs(Steinberg::IPtr<Steinberg::FUnknown> object);

        /**
         * Whether the object supported this interface.
         */
        bool supported;

        /**
         * For `IHostApplication::getName`.
         */
        std::optional<std::u16string> name;

        template <typename S>
        void serialize(S& s) {
            s.value1b(supported);
            s.ext(name, bitsery::ext::StdOptional{},
                  [](S& s, std::u16string& name) {
                      s.text2b(name, std::extent_v<Steinberg::Vst::String128>);
                  });
        }
    };

    /**
     * Instantiate this instance with arguments read from another interface
     * implementation.
     */
    YaHostApplication(const ConstructArgs&& args);

    inline bool supported() const { return arguments.supported; }

    tresult PLUGIN_API getName(Steinberg::Vst::String128 name) override;
    virtual tresult PLUGIN_API createInstance(Steinberg::TUID cid,
                                              Steinberg::TUID _iid,
                                              void** obj) override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop
