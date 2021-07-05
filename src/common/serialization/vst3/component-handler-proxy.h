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

#include "../common.h"
#include "component-handler/component-handler-2.h"
#include "component-handler/component-handler-3.h"
#include "component-handler/component-handler-bus-activation.h"
#include "component-handler/component-handler.h"
#include "component-handler/progress.h"
#include "component-handler/unit-handler-2.h"
#include "component-handler/unit-handler.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * An abstract class that implements `IComponentHandler`, and optionally also
 * all other VST3 interfaces an object passed to
 * `IEditController::setComponentHandler()` might implement. This works exactly
 * the same as `Vst3PluginProxy`, but instead of proxying for an object provided
 * by the plugin we are proxying for the `IComponentHandler*` argument passed to
 * plugin by the host.
 */
class Vst3ComponentHandlerProxy : public YaComponentHandler,
                                  public YaComponentHandler2,
                                  public YaComponentHandler3,
                                  public YaComponentHandlerBusActivation,
                                  public YaProgress,
                                  public YaUnitHandler,
                                  public YaUnitHandler2 {
   public:
    /**
     * These are the arguments for constructing a
     * `Vst3ComponentHandlerProxyImpl`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Read from an existing object. We will try to mimic this object, so
         * we'll support any interfaces this object also supports.
         */
        ConstructArgs(Steinberg::IPtr<FUnknown> object,
                      size_t owner_instance_id) noexcept;

        /**
         * The unique instance identifier of the proxy object instance this
         * component handler has been passed to and thus belongs to. This way we
         * can refer to the correct 'actual' `IComponentHandler` instance when
         * the plugin does a callback.
         */
        native_size_t owner_instance_id;

        YaComponentHandler::ConstructArgs component_handler_args;
        YaComponentHandler2::ConstructArgs component_handler_2_args;
        YaComponentHandler3::ConstructArgs component_handler_3_args;
        YaComponentHandlerBusActivation::ConstructArgs
            component_handler_bus_activation_args;
        YaProgress::ConstructArgs progress_args;
        YaUnitHandler::ConstructArgs unit_handler_args;
        YaUnitHandler2::ConstructArgs unit_handler_2_args;

        template <typename S>
        void serialize(S& s) {
            s.value8b(owner_instance_id);
            s.object(component_handler_args);
            s.object(component_handler_2_args);
            s.object(component_handler_3_args);
            s.object(component_handler_bus_activation_args);
            s.object(progress_args);
            s.object(unit_handler_args);
            s.object(unit_handler_2_args);
        }
    };

    /**
     * Instantiate this instance with arguments read from an actual component
     * handler.
     *
     * @note Since this is passed as part of
     *   `IEditController::setComponentHandler()`, there are no direct
     *   `Construct` or `Destruct` messages. This object's lifetime is bound to
     *   that of the objects they are passed to. If those objects get dropped,
     *   then the host contexts should also be dropped.
     */
    Vst3ComponentHandlerProxy(ConstructArgs&& args) noexcept;

    /**
     * The lifetime of this object should be bound to the object we created it
     * for. When for instance the `Vst3PluginProxy` instance with id `n` gets
     * dropped a corresponding `Vst3ComponentHandlerProxyImpl` should also be
     * dropped.
     */
    virtual ~Vst3ComponentHandlerProxy() noexcept;

    DECLARE_FUNKNOWN_METHODS

    /**
     * Get the instance ID of the owner of this object.
     */
    inline size_t owner_instance_id() const noexcept {
        return arguments.owner_instance_id;
    }

   private:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop
