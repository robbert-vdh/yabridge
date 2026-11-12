// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2026 Robbert van der Helm
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

#include "plugin-factory-proxy.h"

#include <algorithm>
#include <cstring>

#include <pluginterfaces/vst/ivstcomponent.h>

#ifdef WITH_ARA
#include <ARAVST3.h>
#endif

#include "../vst3.h"
#include "ara-main-factory-proxy.h"
#include "plugin-proxy.h"

Vst3PluginFactoryProxyImpl::Vst3PluginFactoryProxyImpl(
    Vst3PluginBridge& bridge,
    Vst3PluginFactoryProxy::ConstructArgs&& args) noexcept
    : Vst3PluginFactoryProxy(std::move(args)), bridge_(bridge) {}

tresult PLUGIN_API
Vst3PluginFactoryProxyImpl::queryInterface(const Steinberg::TUID _iid,
                                           void** obj) {
    const tresult result = Vst3PluginFactoryProxy::queryInterface(_iid, obj);
    bridge_.logger_.log_query_interface("In IPluginFactory::queryInterface()",
                                        result,
                                        Steinberg::FUID::fromTUID(_iid));

    return result;
}

tresult PLUGIN_API
Vst3PluginFactoryProxyImpl::createInstance(Steinberg::FIDString cid,
                                           Steinberg::FIDString _iid,
                                           void** obj) {
    if (!cid || !_iid || !obj) {
        return Steinberg::kInvalidArgument;
    }

    Steinberg::TUID cid_array;
    std::copy(cid, cid + std::extent_v<Steinberg::TUID>, cid_array);

    // I don't think they include a safe way to convert a `FIDString/char*` into
    // a `FUID`, so this will have to do
    const Steinberg::FUID requested_iid = Steinberg::FUID::fromTUID(
        *reinterpret_cast<const Steinberg::TUID*>(&*_iid));

#ifdef WITH_ARA
    // Check if the requested CID maps to a kARAMainFactoryClass entry
    const auto& class_infos = YaPluginFactory3::arguments_.class_infos_1;
    const bool is_ara_main_factory =
        std::any_of(class_infos.begin(), class_infos.end(),
                    [&](const std::optional<Steinberg::PClassInfo>& info) {
                        return info &&
                               std::equal(cid_array,
                                          cid_array +
                                              std::extent_v<Steinberg::TUID>,
                                          info->cid) &&
                               std::strncmp(info->category,
                                            kARAMainFactoryClass,
                                            Steinberg::PClassInfo::kCategorySize) == 0;
                    });

    if (is_ara_main_factory) {
        if (requested_iid != ARA::IMainFactory::iid) {
            bridge_.logger_.log_query_interface(
                "In IPluginFactory::createInstance() for ARA::IMainFactory",
                Steinberg::kNoInterface, requested_iid);
            *obj = nullptr;
            return Steinberg::kNoInterface;
        }

        const NativeUID native_cid(cid_array);
        std::variant<YaAraFactory, UniversalTResult> result =
            bridge_.send_mutually_recursive_message(
                YaMainFactory::Construct{.cid = native_cid});

        return std::visit(
            overload{
                [&](YaAraFactory&& factory) -> tresult {
                    bridge_.logger_.log(
                        "IPluginFactory::createInstance(): created "
                        "ARA::IMainFactory proxy");
                    *obj = static_cast<ARA::IMainFactory*>(
                        new YaMainFactoryImpl(bridge_, std::move(factory)));
                    return Steinberg::kResultOk;
                },
                [&](const UniversalTResult& code) -> tresult {
                    const tresult result = code;
                    bridge_.logger_.log(
                        "WARNING: IPluginFactory::createInstance() for "
                        "ARA::IMainFactory returned " +
                        UniversalTResult(result).string());
                    *obj = nullptr;
                    return result;
                }},
            std::move(result));
    }
#endif  // WITH_ARA

    Vst3PluginProxy::Construct::Interface requested_interface;
    if (requested_iid == Steinberg::Vst::IComponent::iid) {
        requested_interface = Vst3PluginProxy::Construct::Interface::IComponent;
    } else if (requested_iid == Steinberg::Vst::IEditController::iid) {
        requested_interface =
            Vst3PluginProxy::Construct::Interface::IEditController;
    } else {
        // When the host requests an interface we do not (yet) implement, we'll
        // print a recognizable log message
        bridge_.logger_.log_query_interface(
            "In IPluginFactory::createInstance()", Steinberg::kNotImplemented,
            requested_iid);

        *obj = nullptr;
        return Steinberg::kNotImplemented;
    }

    std::variant<Vst3PluginProxy::ConstructArgs, UniversalTResult> result =
        bridge_.send_mutually_recursive_message(Vst3PluginProxy::Construct{
            .cid = cid_array, .requested_interface = requested_interface});

    return std::visit(
        overload{
            [&](Vst3PluginProxy::ConstructArgs&& args) -> tresult {
                // These pointers are scary. The idea here is that we return a
                // newly initialized object (that initializes itself with a
                // reference count of 1), and then the receiving side will use
                // `Steinberg::owned()` to adopt it to an `IPtr<T>`.
                Vst3PluginProxyImpl* proxy_object =
                    new Vst3PluginProxyImpl(bridge_, std::move(args));

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
Vst3PluginFactoryProxyImpl::setHostContext(Steinberg::FUnknown* context) {
    if (context) {
        // We will create a proxy object that that supports all the same
        // interfaces as `context`, and then we'll store `context` in this
        // object. We can then use it to handle callbacks made by the Windows
        // VST3 plugin to this context.
        host_context_ = context;

        // Automatically converted smart pointers for when the plugin performs a
        // callback later
        host_application_ = host_context_;
        plug_interface_support_ = host_context_;

        return bridge_.send_message(YaPluginFactory3::SetHostContext{
            .host_context_args = Vst3HostContextProxy::ConstructArgs(
                host_context_, std::nullopt)});
    } else {
        bridge_.logger_.log(
            "WARNING: Null pointer passed to "
            "'IPluginFactory3::setHostContext()'");
        return Steinberg::kInvalidArgument;
    }
}
