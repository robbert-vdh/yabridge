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

#include "../vst3.h"

class Vst3PlugViewProxyImpl : public Vst3PlugViewProxy {
   public:
    Vst3PlugViewProxyImpl(Vst3PluginBridge& bridge,
                          Vst3PlugViewProxy::ConstructArgs&& args);

    /**
     * When the reference count reaches zero and this destructor is called,
     * we'll send a request to the Wine plugin host to destroy the corresponding
     * object.
     */
    ~Vst3PlugViewProxyImpl();

    /**
     * We'll override the query interface to log queries for interfaces we do
     * not (yet) support.
     */
    tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                      void** obj) override;

    // From `IPlugView`
    tresult PLUGIN_API
    isPlatformTypeSupported(Steinberg::FIDString type) override;
    tresult PLUGIN_API attached(void* parent,
                                Steinberg::FIDString type) override;
    tresult PLUGIN_API removed() override;
    tresult PLUGIN_API onWheel(float distance) override;
    tresult PLUGIN_API onKeyDown(char16 key,
                                 int16 keyCode,
                                 int16 modifiers) override;
    tresult PLUGIN_API onKeyUp(char16 key,
                               int16 keyCode,
                               int16 modifiers) override;
    tresult PLUGIN_API getSize(Steinberg::ViewRect* size) override;
    tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) override;
    tresult PLUGIN_API onFocus(TBool state) override;
    tresult PLUGIN_API setFrame(Steinberg::IPlugFrame* frame) override;
    tresult PLUGIN_API canResize() override;
    tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* rect) override;

    // From `IParameterFinder`
    tresult PLUGIN_API
    findParameter(int32 xPos,
                  int32 yPos,
                  Steinberg::Vst::ParamID& resultTag /*out*/) override;

    /**
     * The `IPlugFrame` object passed by the host passed to us in
     * `IPlugView::setFrame()`. When the plugin makes a callback on the
     * `IPlugFrame` proxy object, we'll pass the call through to this object.
     */
    Steinberg::IPtr<Steinberg::IPlugFrame> plug_frame;

   private:
    Vst3PluginBridge& bridge;
};
