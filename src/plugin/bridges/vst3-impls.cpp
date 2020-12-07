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

#include "vst3-impls.h"

YaPluginFactoryPluginImpl::YaPluginFactoryPluginImpl(Vst3PluginBridge& bridge)
    : bridge(bridge) {}

tresult PLUGIN_API
YaPluginFactoryPluginImpl::createInstance(Steinberg::FIDString cid,
                                          Steinberg::FIDString _iid,
                                          void** obj) {
    // TODO: This should:
    //       1. Check which interface `_iid` belongs to. Let's call this
    //          interface `T`. If we do not (yet) support it, then we should log
    //          it and return `Steinberg::kNotImplemented`.
    //       2. Send a control message to the Wine plugin host to instantiate a
    //          new object of type `T` with `cid` and `_iid` as parameters.
    //       3. On the Wine side this calls `createIntance()` on the module's
    //          factory with thsoe same `cid` and `_iid` arguments.
    //       4. It this was successful, we'll assign this object a unique number
    //          (by just doing a fetch-and-add on an atomic size_t) so we can
    //          refer to it and add it to an `std::map<size_t, IP tr<T>`, where
    //          `T` is the _original_ object (we don't have to and shouldn't
    //          wrap it).
    //       5. We'll copy over any payload data into a `YaT` **which includes
    //          that unique identifier we generated** and send it back to the
    //          plugin.
    //       6. On the plugin's side we'll create a new `YaTPluginImpl` inside
    //          of a VST smart pointer and deserialize the `YaT` we got sent
    //          into that. We then write that smart pointer into `obj`. We don't
    //          have to keep track of these objects on the plugin side and the
    //          reference counting pointers will cause everything to clean up
    //          after itself.
    //       7. Since those `YaTPluginImpl` objects we'll return from
    //          `createInstance()` will have a reference to `Vst3PluginBridge`,
    //          they can also send control messages themselves.

    return Steinberg::kNotImplemented;
}

tresult PLUGIN_API
YaPluginFactoryPluginImpl::setHostContext(Steinberg::FUnknown* /*context*/) {
    // TODO: The docs don't clearly specify what this should be doing, but from
    //       what I've seen this is only used to pass a `IHostApplication`
    //       instance. That's used to allow the plugin to create objects in the
    //       host.
    return Steinberg::kNotImplemented;
}
