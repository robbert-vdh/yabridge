// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2023 Robbert van der Helm
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

class Vst3PlugFrameProxyImpl : public Vst3PlugFrameProxy {
   public:
    Vst3PlugFrameProxyImpl(Vst3Bridge& bridge,
                           Vst3PlugFrameProxy::ConstructArgs&& args) noexcept;

    /**
     * We'll override the query interface to log queries for interfaces we do
     * not (yet) support.
     */
    tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                      void** obj) override;

    // From `IPlugFrame`
    tresult PLUGIN_API resizeView(Steinberg::IPlugView* view,
                                  Steinberg::ViewRect* newSize) override;

   private:
    Vst3Bridge& bridge_;
};
