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

#include "plugin-factory.h"

#include <pluginterfaces/vst/ivstcomponent.h>

#include "component.h"

YaPluginFactoryPluginImpl::YaPluginFactoryPluginImpl(
    Vst3PluginBridge& bridge,
    YaPluginFactory::ConstructArgs&& args)
    : YaPluginFactory(std::move(args)), bridge(bridge) {}

tresult PLUGIN_API
YaPluginFactoryPluginImpl::createInstance(Steinberg::FIDString cid,
                                          Steinberg::FIDString _iid,
                                          void** obj) {
    // TODO: Do the same thing for other types

    // These arw pointers are scary. The idea here is that we return a newly
    // initialized object (that initializes itself with a reference count of 1),
    // and then the receiving side will use `Steinberg::owned()` to adopt it to
    // an `IPtr<T>`.
    ArrayUID cid_array;
    std::copy(cid, cid + sizeof(Steinberg::TUID), cid_array.begin());
    if (Steinberg::FIDStringsEqual(_iid, Steinberg::Vst::IComponent::iid)) {
        std::variant<YaComponent::ConstructArgs, UniversalTResult> result =
            bridge.send_message(YaComponent::Construct{.cid = cid_array});
        return std::visit(
            overload{
                [&](YaComponent::ConstructArgs&& args) -> tresult {
                    *obj = static_cast<Steinberg::Vst::IComponent*>(
                        new YaComponentPluginImpl(bridge, std::move(args)));
                    return Steinberg::kResultOk;
                },
                [&](const UniversalTResult& code) { return code.native(); }},
            std::move(result));
    } else {
        // When the host requests an interface we do not (yet) implement, we'll
        // print a recognizable log message. I don't think they include a safe
        // way to convert a `FIDString/char*` into a `FUID`, so this will have
        // to do.
        std::optional<Steinberg::FUID> uid;
        constexpr size_t uid_size = sizeof(Steinberg::TUID);
        if (_iid && strnlen(_iid, uid_size + 1) == uid_size) {
            uid = Steinberg::FUID::fromTUID(
                *reinterpret_cast<const Steinberg::TUID*>(&_iid));
        }

        bridge.logger.log_unknown_interface(
            "In IPluginFactory::createInstance()", uid);

        return Steinberg::kNotImplemented;
    }
}

tresult PLUGIN_API
YaPluginFactoryPluginImpl::setHostContext(Steinberg::FUnknown* context) {
    // This `context` will likely be an `IHostApplication`. If it is, we will
    // store it for future calls, create a proxy object on the Wine side, and
    // then pass it to the Windows VST3 plugin's plugin factory using the same
    // function. If we get passed anything else we'll just return instead since
    // there's nothing we can do with it.
    host_application_context = context;

    if (host_application_context) {
        YaHostApplication::ConstructArgs host_application_context_args(
            host_application_context, std::nullopt);

        return bridge
            .send_message(YaPluginFactory::SetHostContext{
                .host_application_context_args =
                    std::move(host_application_context_args)})
            .native();
    } else {
        bridge.logger.log_unknown_interface(
            "In IPluginFactory3::setHostContext(), ignoring",
            context ? std::optional(context->iid) : std::nullopt);
        return Steinberg::kNotImplemented;
    }
}
