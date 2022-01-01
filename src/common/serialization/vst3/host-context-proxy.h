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

#include "../common.h"
#include "host-context/host-application.h"
#include "host-context/plug-interface-support.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * An abstract class that optionally implements all interfaces a `context`
 * object passed to `IPluginBase::intialize()` or
 * `IPluginFactory3::setHostContext()` might implement. This works exactly the
 * same as `Vst3PluginProxy`, but instead of proxying for an object provided by
 * the plugin we are proxying for the `FUnknown*` argument passed to plugin by
 * the host. When we are proxying for a host context object passed to
 * `IPluginBase::initialize()` we'll keep track of the object instance ID the
 * actual context object belongs to.
 */
class Vst3HostContextProxy : public YaHostApplication,
                             public YaPlugInterfaceSupport {
   public:
    /**
     * These are the arguments for constructing a
     * `Vst3HostContextProxyImpl`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Read from an existing object. We will try to mimic this object, so
         * we'll support any interfaces this object also supports.
         */
        ConstructArgs(Steinberg::IPtr<FUnknown> object,
                      std::optional<size_t> owner_instance_id) noexcept;

        /**
         * The unique instance identifier of the proxy object instance this host
         * context has been passed to and thus belongs to. If we are handling
         * When handling `IPluginFactory::setHostContext()` this will be empty.
         */
        std::optional<native_size_t> owner_instance_id;

        YaHostApplication::ConstructArgs host_application_args;
        YaPlugInterfaceSupport::ConstructArgs plug_interface_support_args;

        template <typename S>
        void serialize(S& s) {
            s.ext(owner_instance_id, bitsery::ext::InPlaceOptional{},
                  [](S& s, native_size_t& instance_id) {
                      s.value8b(instance_id);
                  });
            s.object(host_application_args);
            s.object(plug_interface_support_args);
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
     */
    Vst3HostContextProxy(ConstructArgs&& args) noexcept;

    /**
     * The lifetime of this object should be bound to the object we created it
     * for. When for instance the `Vst3PluginProxy` instance with id `n` gets
     * dropped a corresponding `Vst3HostContextProxyImpl` should also be
     * dropped.
     */
    virtual ~Vst3HostContextProxy() noexcept;

    DECLARE_FUNKNOWN_METHODS

    /**
     * Get the instance ID of the owner of this object, if this is not the
     * global host context passed to the module's plugin factory.
     */
    inline std::optional<size_t> owner_instance_id() const noexcept {
        return arguments_.owner_instance_id;
    }

   private:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop
