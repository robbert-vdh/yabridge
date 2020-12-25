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

#include "connection-point-proxy.h"

Vst3ConnectionPointProxy::Vst3ConnectionPointProxy(const ConstructArgs&& args)
    : YaConnectionPoint(std::move(args.connection_point_args)),
      arguments(std::move(args)){FUNKNOWN_CTOR}

      Vst3ConnectionPointProxy::~Vst3ConnectionPointProxy() {
    FUNKNOWN_DTOR
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
IMPLEMENT_REFCOUNT(Vst3ConnectionPointProxy)
#pragma GCC diagnostic pop

tresult PLUGIN_API
Vst3ConnectionPointProxy::queryInterface(Steinberg::FIDString _iid,
                                         void** obj) {
    if (YaConnectionPoint::supported()) {
        QUERY_INTERFACE(_iid, obj, Steinberg::FUnknown::iid,
                        Steinberg::Vst::IConnectionPoint)
        QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IConnectionPoint::iid,
                        Steinberg::Vst::IConnectionPoint)
    }

    *obj = nullptr;
    return Steinberg::kNoInterface;
}
