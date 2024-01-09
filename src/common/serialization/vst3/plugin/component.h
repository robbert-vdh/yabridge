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

#include <pluginterfaces/vst/ivstcomponent.h>

#include "../../../audio-shm.h"
#include "../../../bitsery/ext/in-place-optional.h"
#include "../../common.h"
#include "../base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IComponent` for serialization purposes. This is instantiated as
 * part of `Vst3PluginProxy`. Event though `IComponent` inherits from
 * `IPlguinBase`, we'll implement that separately in `YaPluginBase` because
 * `IEditController` also inherits from `IPluginBase`.
 */
class YaComponent : public Steinberg::Vst::IComponent {
   public:
    /**
     * These are the arguments for creating a `YaComponent`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements `IComponent` and
         * read arguments from it.
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
    YaComponent(ConstructArgs&& args) noexcept;

    virtual ~YaComponent() noexcept = default;

    inline bool supported() const noexcept { return arguments_.supported; }

    /**
     * The response code and returned CID for a call to
     * `IComponent::getControllerClassId()`.
     */
    struct GetControllerClassIdResponse {
        UniversalTResult result;
        WineUID editor_cid;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.object(editor_cid);
        }
    };

    /**
     * Message to pass through a call to `IComponent::getControllerClassId()` to
     * the Wine plugin host.
     */
    struct GetControllerClassId {
        using Response = GetControllerClassIdResponse;

        native_size_t instance_id;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
        }
    };

    virtual tresult PLUGIN_API
    getControllerClassId(Steinberg::TUID classId) override = 0;

    /**
     * Message to pass through a call to `IComponent::setIoMode(mode)` to the
     * Wine plugin host.
     */
    struct SetIoMode {
        using Response = UniversalTResult;

        native_size_t instance_id;

        Steinberg::Vst::IoMode mode;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(mode);
        }
    };

    virtual tresult PLUGIN_API
    setIoMode(Steinberg::Vst::IoMode mode) override = 0;

    /**
     * Message to pass through a call to `IComponent::getBusCount(type, dir)` to
     * the Wine plugin host.
     */
    struct GetBusCount {
        using Response = PrimitiveResponse<int32>;

        native_size_t instance_id;

        Steinberg::Vst::BusType type;
        Steinberg::Vst::BusDirection dir;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(type);
            s.value4b(dir);
        }
    };

    virtual int32 PLUGIN_API
    getBusCount(Steinberg::Vst::MediaType type,
                Steinberg::Vst::BusDirection dir) override = 0;

    /**
     * The response code and returned bus information for a call to
     * `IComponent::getBusInfo(type, dir, index, &bus)`.
     */
    struct GetBusInfoResponse {
        UniversalTResult result;
        Steinberg::Vst::BusInfo bus;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.object(bus);
        }
    };

    /**
     * Message to pass through a call to `IComponent::getBusInfo(type, dir,
     * index, &bus)` to the Wine plugin host.
     */
    struct GetBusInfo {
        using Response = GetBusInfoResponse;

        native_size_t instance_id;

        Steinberg::Vst::BusType type;
        Steinberg::Vst::BusDirection dir;
        int32 index;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(type);
            s.value4b(dir);
            s.value4b(index);
        }
    };

    virtual tresult PLUGIN_API
    getBusInfo(Steinberg::Vst::MediaType type,
               Steinberg::Vst::BusDirection dir,
               int32 index,
               Steinberg::Vst::BusInfo& bus /*out*/) override = 0;

    /**
     * The response code and returned routing information for a call to
     * `IComponent::getRoutingInfo(in_info, &out_info)`.
     */
    struct GetRoutingInfoResponse {
        UniversalTResult result;
        Steinberg::Vst::RoutingInfo out_info;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.object(out_info);
        }
    };

    /**
     * Message to pass through a call to `IComponent::getRoutingInfo(in_info,
     * &out_info)` to the Wine plugin host.
     */
    struct GetRoutingInfo {
        using Response = GetRoutingInfoResponse;

        native_size_t instance_id;

        Steinberg::Vst::RoutingInfo in_info;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.object(in_info);
        }
    };

    virtual tresult PLUGIN_API
    getRoutingInfo(Steinberg::Vst::RoutingInfo& inInfo,
                   Steinberg::Vst::RoutingInfo& outInfo /*out*/) override = 0;

    /**
     * Message to pass through a call to `IComponent::activateBus(type, dir,
     * index, state)` to the Wine plugin host.
     */
    struct ActivateBus {
        using Response = UniversalTResult;

        native_size_t instance_id;

        Steinberg::Vst::MediaType type;
        Steinberg::Vst::BusDirection dir;
        int32 index;
        TBool state;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value4b(type);
            s.value4b(dir);
            s.value4b(index);
            s.value1b(state);
        }
    };

    virtual tresult PLUGIN_API activateBus(Steinberg::Vst::MediaType type,
                                           Steinberg::Vst::BusDirection dir,
                                           int32 index,
                                           TBool state) override = 0;

    /**
     * The response code and written state for a call to
     * `IAudioProcessor::setActive(state)`.
     */
    struct SetActiveResponse {
        UniversalTResult result;
        std::optional<AudioShmBuffer::Config> updated_audio_buffers_config;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.ext(updated_audio_buffers_config,
                  bitsery::ext::InPlaceOptional{});
        }
    };

    /**
     * Message to pass through a call to `IComponent::setActive(state)` to the
     * Wine plugin host.
     *
     * NOTE: REAPER may change a plugin's bus arrangements after the processing
     *       has been set up, so we need to check for this on every
     *       `setActive()` call.
     */
    struct SetActive {
        using Response = SetActiveResponse;

        native_size_t instance_id;

        TBool state;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.value1b(state);
        }
    };

    virtual tresult PLUGIN_API setActive(TBool state) override = 0;

    // `setState()` and `getState()` are defiend in both `IComponent` and
    // `IEditController`. Since an object can only ever implement one or the
    // other, the messages for calling either are defined directly on
    // `Vst3PluginProxy`.
    virtual tresult PLUGIN_API
    setState(Steinberg::IBStream* state) override = 0;
    virtual tresult PLUGIN_API
    getState(Steinberg::IBStream* state) override = 0;

   protected:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop
