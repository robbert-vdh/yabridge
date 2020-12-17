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

#include <bitsery/ext/std_variant.h>
#include <pluginterfaces/vst/ivstcomponent.h>

#include "../common.h"
#include "base.h"
#include "host-application.h"
#include "plugin/audio-processor.h"
#include "plugin/component.h"
#include "plugin/edit-controller.h"
#include "plugin/plugin-base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * An abstract class that optionally implements all VST3 interfaces a plugin
 * object could implement. A more in depth explanation can be found in
 * `docs/vst3.md`, but the way this works is that we begin with an `FUnknown`
 * pointer from the Windows VST3 plugin obtained by a call to
 * `IPluginFactory::createInstance()` (with an interface decided by the host).
 * We then go through all the plugin interfaces and check whether that object
 * supports them one by one. For each supported interface we remember that the
 * plugin supports it, and we'll optionally write down some static data (such as
 * the edit controller cid) that can't change over the lifetime of the
 * application. On the plugin side we then return a `Vst3PluginProxyImpl` object
 * that contains all of this information about interfaces the object we're
 * proxying might support. This way we can allow casts to all of those object
 * types in `queryInterface()`, essentially perfectly mimicing the original
 * object.
 *
 * This monolith approach is also important when it comes to `IConnectionPoint`.
 * The host should be able to connect arbitrary objects together, and the plugin
 * can then use the query interface smart pointer casting system to cast those
 * objects to the types they want. By having a huge monolithic class that
 * implements any interface such an object might also implement, we can allow
 * perfect proxying behaviour for connecting components.
 */
class Vst3PluginProxy : public YaAudioProcessor,
                        public YaComponent,
                        public YaEditController2,
                        public YaPluginBase {
   public:
    /**
     * These are the arguments for creating a `Vst3PluginProxyImpl`.
     */
    struct ConstructArgs {
        ConstructArgs();

        /**
         * Read from an existing object. We will try to mimic this object, so
         * we'll support any interfaces this object also supports.
         */
        ConstructArgs(Steinberg::IPtr<FUnknown> object, size_t instance_id);

        /**
         * The unique identifier for this specific object instance.
         */
        native_size_t instance_id;

        YaAudioProcessor::ConstructArgs audio_processor_args;
        YaComponent::ConstructArgs component_args;
        YaEditController2::ConstructArgs edit_controller_2_args;
        YaPluginBase::ConstructArgs plugin_base_args;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.object(audio_processor_args);
            s.object(edit_controller_2_args);
            s.object(component_args);
            s.object(plugin_base_args);
        }
    };

    /**
     * Message to request the Wine plugin host to instantiate a new IComponent
     * to pass through a call to `IComponent::createInstance(cid,
     * <requested_interface>::iid, ...)`.
     */
    struct Construct {
        using Response = std::variant<ConstructArgs, UniversalTResult>;

        ArrayUID cid;

        /**
         * The interface the host was trying to instantiate an object for.
         * Technically the host can create any kind of object, but these are the
         * objects that are actually used.
         */
        enum class Interface {
            IComponent,
            IEditController,
        };

        Interface requested_interface;

        template <typename S>
        void serialize(S& s) {
            s.container1b(cid);
            s.value4b(requested_interface);
        }
    };

    /**
     * Instantiate this object instance with arguments read from another
     * interface implementation.
     */
    Vst3PluginProxy(const ConstructArgs&& args);

    /**
     * Message to request the Wine plugin host to destroy this object instance
     * with the given instance ID. Sent from the destructor of
     * `Vst3PluginProxyImpl`. This will cause all smart pointers to the actual
     * object in the Wine plugin host to be dropped.
     */
    struct Destruct {
        using Response = Ack;

        native_size_t instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
        }
    };

    /**
     * @remark The plugin side implementation should send a control message to
     *   clean up the instance on the Wine side in its destructor.
     */
    virtual ~Vst3PluginProxy() = 0;

    DECLARE_FUNKNOWN_METHODS

    // We'll define messages for functions that have identical definitions in
    // multiple interfaces below. When the Wine plugin host process handles
    // these it should check which of the interfaces is supported on the host.

    /**
     * Message to pass through a call to
     * `{IComponent,IEditController}::setState(state)` to the Wine plugin host.
     */
    struct SetState {
        using Response = UniversalTResult;

        native_size_t instance_id;

        VectorStream state;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.object(state);
        }
    };

    /**
     * The response code and written state for a call to
     * `{IComponent,IEditController}::getState(state)`.
     */
    struct GetStateResponse {
        UniversalTResult result;
        VectorStream updated_state;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.object(updated_state);
        }
    };

    /**
     * Message to pass through a call to
     * `{IComponent,IEditController}::getState(state)` to the Wine plugin host.
     */
    struct GetState {
        using Response = GetStateResponse;

        native_size_t instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
        }
    };

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop

template <typename S>
void serialize(
    S& s,
    std::variant<Vst3PluginProxy::ConstructArgs, UniversalTResult>& result) {
    s.ext(result, bitsery::ext::StdVariant{});
}
