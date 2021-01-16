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

#include "plugin-factory.h"

#include <pluginterfaces/vst/ivstcomponent.h>

#include "plugin-proxy.h"

YaPluginFactoryImpl::YaPluginFactoryImpl(Vst3PluginBridge& bridge,
                                         YaPluginFactory::ConstructArgs&& args)
    : YaPluginFactory(std::move(args)), bridge(bridge) {}

tresult PLUGIN_API
YaPluginFactoryImpl::createInstance(Steinberg::FIDString cid,
                                    Steinberg::FIDString _iid,
                                    void** obj) {
    // Class IDs may be padded with null bytes
    constexpr size_t uid_size = sizeof(Steinberg::TUID);
    if (!cid || !_iid || !obj || strnlen(_iid, uid_size) < uid_size) {
        return Steinberg::kInvalidArgument;
    }

    ArrayUID cid_array;
    std::copy(cid, cid + std::extent_v<Steinberg::TUID>, cid_array.begin());

    // FIXME: `_iid` in Bitwig Studio 3.3.1 is not null terminated, and the
    //        comparison below will thus fail since the strings have different
    //        lengths. Since it looks like the module implementation that comes
    //        with the SDK has this same issue I think it might just be a case
    //        of Steinberg not following its own specifications.
    std::string iid_string(_iid, uid_size);

    Vst3PluginProxy::Construct::Interface requested_interface;
    if (Steinberg::FIDStringsEqual(iid_string.c_str(),
                                   Steinberg::Vst::IComponent::iid)) {
        requested_interface = Vst3PluginProxy::Construct::Interface::IComponent;
    } else if (Steinberg::FIDStringsEqual(
                   iid_string.c_str(), Steinberg::Vst::IEditController::iid)) {
        requested_interface =
            Vst3PluginProxy::Construct::Interface::IEditController;
    } else {
        // When the host requests an interface we do not (yet) implement, we'll
        // print a recognizable log message. I don't think they include a safe
        // way to convert a `FIDString/char*` into a `FUID`, so this will have
        // to do.
        const Steinberg::FUID uid = Steinberg::FUID::fromTUID(
            *reinterpret_cast<const Steinberg::TUID*>(&*_iid));
        bridge.logger.log_query_interface("In IPluginFactory::createInstance()",
                                          Steinberg::kNotImplemented, uid);

        *obj = nullptr;
        return Steinberg::kNotImplemented;
    }

    std::variant<Vst3PluginProxy::ConstructArgs, UniversalTResult> result =
        bridge.send_message(Vst3PluginProxy::Construct{
            .cid = cid_array, .requested_interface = requested_interface});

    return std::visit(
        overload{
            [&](Vst3PluginProxy::ConstructArgs&& args) -> tresult {
                // These pointers are scary. The idea here is that we return a
                // newly initialized object (that initializes itself with a
                // reference count of 1), and then the receiving side will use
                // `Steinberg::owned()` to adopt it to an `IPtr<T>`.
                Vst3PluginProxyImpl* proxy_object =
                    new Vst3PluginProxyImpl(bridge, std::move(args));

                // We return a properly downcasted version of the proxy object
                // we just created
                switch (requested_interface) {
                    case Vst3PluginProxy::Construct::Interface::IComponent:
                        *obj = static_cast<Steinberg::Vst::IComponent*>(
                            proxy_object);
                        break;
                    case Vst3PluginProxy::Construct::Interface::IEditController:
                        *obj = static_cast<Steinberg::Vst::IEditController*>(
                            proxy_object);
                        break;
                }

                return Steinberg::kResultOk;
            },
            [&](const UniversalTResult& code) -> tresult { return code; }},
        std::move(result));
}

tresult PLUGIN_API
YaPluginFactoryImpl::setHostContext(Steinberg::FUnknown* context) {
    if (context) {
        // We will create a proxy object that that supports all the same
        // interfaces as `context`, and then we'll store `context` in this
        // object. We can then use it to handle callbacks made by the Windows
        // VST3 plugin to this context.
        host_context = context;

        // Automatically converted smart pointers for when the plugin performs a
        // callback later
        host_application = host_context;
        plug_interface_support = host_context;

        return bridge.send_message(YaPluginFactory::SetHostContext{
            .host_context_args = Vst3HostContextProxy::ConstructArgs(
                host_context, std::nullopt)});
    } else {
        bridge.logger.log(
            "WARNING: Null pointer passed to "
            "'IPluginFactory3::setHostContext()'");
        return Steinberg::kInvalidArgument;
    }
}
