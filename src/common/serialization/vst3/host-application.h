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

#include "../common.h"
#include "base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IHostApplication` for serialization purposes. See `README.md`
 * for more information on how this works. This is used both to proxy the host
 * application context passed during `IPluginBase::intialize()` as well as for
 * `IPluginFactory3::setHostContext()`. This interface is thus implemented on
 * both the native plugin side as well as the Wine plugin host side.
 *
 * TODO: When implementing more host interfaces, also rework this into a
 *       monolithic proxy class like with the plugin.
 */
class YaHostApplication : public Steinberg::Vst::IHostApplication {
   public:
    /**
     * These are the arguments for creating a
     * `YaYaHostApplication{Plugin,Host}Impl`.
     */
    struct ConstructArgs {
        ConstructArgs();

        /**
         * Read arguments from an existing implementation.
         */
        ConstructArgs(Steinberg::IPtr<Steinberg::Vst::IHostApplication> context,
                      std::optional<size_t> component_instance_id);

        /**
         * The unique instance identifier of the component this host context has
         * been passed to and thus belongs to, if we are handling
         * `IpluginBase::initialize()`. When handling
         * `IPluginFactory::setHostContext()` this will be empty.
         */
        std::optional<native_size_t> component_instance_id;

        /**
         * For `IHostApplication::getName`.
         */
        std::optional<std::u16string> name;

        template <typename S>
        void serialize(S& s) {
            s.ext(component_instance_id, bitsery::ext::StdOptional{},
                  [](S& s, native_size_t& instance_id) {
                      s.value8b(instance_id);
                  });
            s.ext(name, bitsery::ext::StdOptional{},
                  [](S& s, std::u16string& name) {
                      s.text2b(name, std::extent_v<Steinberg::Vst::String128>);
                  });
        }
    };

    /**
     * Instantiate this instance with arguments read from an actual host
     * context.
     *
     * @note Since this is passed as part of `IPluginBase::intialize()` and
     *   `IPluginFactory3::setHostContext()`, there are no direct `Construct` or
     *   `Destruct` messages. This object's lifetime is bound to that of the
     *   objects they are passed to. If those objects get dropped, then the host
     *   contexts should also be dropped.
     *
     * TODO: Check if this ends up working out this way
     */
    YaHostApplication(const ConstructArgs&& args);

    /**
     * The lifetime of this object should be bound to the object we created it
     * for. When for instance the `Vst3PluginProxy` instance with id `n` gets
     * dropped and we also track a `YaHostApplicationImpl` for the component
     * with instance id `n`, then that should also be dropped.
     */
    virtual ~YaHostApplication();

    DECLARE_FUNKNOWN_METHODS

    // From `IHostApplication`
    tresult PLUGIN_API getName(Steinberg::Vst::String128 name) override;
    virtual tresult PLUGIN_API createInstance(Steinberg::TUID cid,
                                              Steinberg::TUID _iid,
                                              void** obj) override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop
