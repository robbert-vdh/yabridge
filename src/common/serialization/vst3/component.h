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

#include <pluginterfaces/vst/ivstcomponent.h>

using Steinberg::TBool, Steinberg::int32, Steinberg::tresult;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IComponent` for serialization purposes. See `README.md` for
 * more information on how this works. On the Wine plugin host side this is only
 * used for serialization, and on the plugin side have an implementation that
 * can send control messages.
 *
 * We might be able to do some caching here with the buss infos, but since that
 * sounds like a huge potential source of errors we'll just do pure callbacks
 * for everything other than the edit controller's class ID.
 */
class YaComponent : public Steinberg::Vst::IComponent {
   public:
    YaComponent();

    /**
     * Create a copy of an existing component.
     */
    explicit YaComponent(Steinberg::IPtr<Steinberg::Vst::IComponent> component);

    virtual ~YaComponent();

    DECLARE_FUNKNOWN_METHODS

    // From `IPluginBase`
    virtual tresult PLUGIN_API initialize(FUnknown* context) override = 0;
    virtual tresult PLUGIN_API terminate() override = 0;

    // From `IComponent`
    tresult PLUGIN_API getControllerClassId(Steinberg::TUID classId) override;
    virtual tresult PLUGIN_API
    setIoMode(Steinberg::Vst::IoMode mode) override = 0;
    virtual int32 PLUGIN_API
    getBusCount(Steinberg::Vst::MediaType type,
                Steinberg::Vst::BusDirection dir) override = 0;
    virtual tresult PLUGIN_API
    getBusInfo(Steinberg::Vst::MediaType type,
               Steinberg::Vst::BusDirection dir,
               int32 index,
               Steinberg::Vst::BusInfo& bus /*out*/) override = 0;
    virtual tresult PLUGIN_API
    getRoutingInfo(Steinberg::Vst::RoutingInfo& inInfo,
                   Steinberg::Vst::RoutingInfo& outInfo /*out*/) override = 0;
    virtual tresult PLUGIN_API activateBus(Steinberg::Vst::MediaType type,
                                           Steinberg::Vst::BusDirection dir,
                                           int32 index,
                                           TBool state) override = 0;
    virtual tresult PLUGIN_API setActive(TBool state) override = 0;
    virtual tresult PLUGIN_API
    setState(Steinberg::IBStream* state) override = 0;
    virtual tresult PLUGIN_API
    getState(Steinberg::IBStream* state) override = 0;

    template <typename S>
    void serialize(S& s) {
        s.container1b(edit_controller_cid);
    }

   private:
    /**
     * The class ID of this component's corresponding editor controller.
     */
    Steinberg::TUID edit_controller_cid;

    // TODO: As explained in a few other places, `YaComponent` objects should be
    //       assigned a unique ID for identification
};

#pragma GCC diagnostic pop
