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

#include <bitsery/ext/std_optional.h>
#include <bitsery/traits/string.h>
#include <pluginterfaces/base/ipluginbase.h>

#include "../base.h"
#include "../host-context-proxy.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IPluginFactory{1,2,3}` for serialization purposes. This is
 * instantiated as part of `Vst3PluginFactoryProxy`.
 */
class YaPluginFactory3 : public Steinberg::IPluginFactory3 {
   public:
    /**
     * These are the arguments for creating a `YaPluginFactory3`. All class
     * infos in all available formats are read from the plugin so the host can
     * query them.
     */
    struct ConstructArgs {
        ConstructArgs();

        /**
         * Check whether an existing implementation implements
         * `IPluginFactory1`, `IPluginFactory2`, and ``IPluginFactory3`` and
         * read arguments from it.
         */
        ConstructArgs(Steinberg::IPtr<Steinberg::FUnknown> object);

        /**
         * Whether the object supported `IPluginFactory`.
         */
        bool supports_plugin_factory = false;

        /**
         * Whether the object supported `IPluginFactory2`.
         */
        bool supports_plugin_factory_2 = false;

        /**
         * Whether the object supported `IPluginFactory3`.
         */
        bool supports_plugin_factory_3 = false;

        /**
         * For `IPluginFactory::getFactoryInfo`.
         */
        std::optional<Steinberg::PFactoryInfo> factory_info;

        /**
         * For `IPluginFactory::countClasses`.
         */
        int num_classes;

        /**
         * For `IPluginFactory::getClassInfo`. We need to store all four class
         * info versions if the plugin can provide them since we don't know
         * which version of the interface the host will use. Will be
         * `std::nullopt` if the plugin doesn't return a class info.
         *
         * NOTE: We'll have already converted all returned class IDs to native
         *       class IDs using `WienUID::to_native_uid()` for cross-platform
         *       compatibility. This applies to all `class_info_*` fields here.
         */
        std::vector<std::optional<Steinberg::PClassInfo>> class_infos_1;

        /**
         * For `IPluginFactory2::getClassInfo2`, works the same way as the
         * above.
         */
        std::vector<std::optional<Steinberg::PClassInfo2>> class_infos_2;

        /**
         * For `IPluginFactory3::getClassInfoUnicode`, works the same way as the
         * above.
         */
        std::vector<std::optional<Steinberg::PClassInfoW>> class_infos_unicode;

        template <typename S>
        void serialize(S& s) {
            s.value1b(supports_plugin_factory);
            s.value1b(supports_plugin_factory_2);
            s.value1b(supports_plugin_factory_3);
            s.ext(factory_info, bitsery::ext::StdOptional{});
            s.value4b(num_classes);
            s.container(class_infos_1, 2048,
                        [](S& s, std::optional<Steinberg::PClassInfo>& info) {
                            s.ext(info, bitsery::ext::StdOptional{});
                        });
            s.container(class_infos_2, 2048,
                        [](S& s, std::optional<Steinberg::PClassInfo2>& info) {
                            s.ext(info, bitsery::ext::StdOptional{});
                        });
            s.container(class_infos_unicode, 2048,
                        [](S& s, std::optional<Steinberg::PClassInfoW>& info) {
                            s.ext(info, bitsery::ext::StdOptional{});
                        });
        }
    };

    /**
     * Instantiate this instance with arguments read from the Windows VST3
     * plugin's plugin factory.
     */
    YaPluginFactory3(const ConstructArgs&& args);

    inline bool supports_plugin_factory() const {
        return arguments.supports_plugin_factory;
    }

    inline bool supports_plugin_factory_2() const {
        return arguments.supports_plugin_factory_2;
    }

    inline bool supports_plugin_factory_3() const {
        return arguments.supports_plugin_factory_3;
    }

    // All of these functiosn returning class information are fetched once on
    // the Wine side since they'll be static so we can just copy over the
    // responses

    // From `IPluginFactory`
    tresult PLUGIN_API getFactoryInfo(Steinberg::PFactoryInfo* info) override;
    int32 PLUGIN_API countClasses() override;
    tresult PLUGIN_API getClassInfo(Steinberg::int32 index,
                                    Steinberg::PClassInfo* info) override;
    /**
     * See the implementation in `Vst3PluginFactoryProxyImpl` for how this is
     * handled. We'll create new managed `Vst3PluginProxy` objects from here.
     */
    virtual tresult PLUGIN_API createInstance(Steinberg::FIDString cid,
                                              Steinberg::FIDString _iid,
                                              void** obj) override = 0;

    // From `IPluginFactory2`
    tresult PLUGIN_API getClassInfo2(int32 index,
                                     Steinberg::PClassInfo2* info) override;

    // From `IPluginFactory3`
    tresult PLUGIN_API
    getClassInfoUnicode(int32 index, Steinberg::PClassInfoW* info) override;

    /**
     * Message to pass through a call to `IPluginFactory3::setHostContext()` to
     * the Wine plugin host. A `Vst3HostContextProxy` should be created on the
     * Wine plugin host and then passed as an argument to
     * `IPluginFactory3::setHostContext()`.
     */
    struct SetHostContext {
        using Response = UniversalTResult;

        Vst3HostContextProxy::ConstructArgs host_context_args;

        template <typename S>
        void serialize(S& s) {
            s.object(host_context_args);
        }
    };

    virtual tresult PLUGIN_API
    setHostContext(Steinberg::FUnknown* context) override = 0;

   protected:
    ConstructArgs arguments;
};

#pragma GCC diagnostic pop

// Serialization functions have to live in the same namespace as the objects
// they're serializing
namespace Steinberg {
template <typename S>
void serialize(S& s, PClassInfo& class_info) {
    s.container1b(class_info.cid);
    s.value4b(class_info.cardinality);
    s.text1b(class_info.category);
    s.text1b(class_info.name);
}

template <typename S>
void serialize(S& s, PClassInfo2& class_info) {
    s.container1b(class_info.cid);
    s.value4b(class_info.cardinality);
    s.text1b(class_info.category);
    s.text1b(class_info.name);
    s.value4b(class_info.classFlags);
    s.text1b(class_info.subCategories);
    s.text1b(class_info.vendor);
    s.text1b(class_info.version);
    s.text1b(class_info.sdkVersion);
}

template <typename S>
void serialize(S& s, PClassInfoW& class_info) {
    s.container1b(class_info.cid);
    s.value4b(class_info.cardinality);
    s.text1b(class_info.category);
    // FIXME: Bitsery uses `std::char_traits<wchar_t>::length()` under the hood
    //        for `text2b()` on the Wine side, and under winegcc this function
    //        this length is incorrect. As a workaround we're just serializing
    //        the entire container. This applies to every place where we use
    //        `container2b()` to serialize a `String128`, so if we end up fixing
    //        this we should replace all of the instances of `container2b()`
    //        that serialize a `String128`.
    s.container2b(class_info.name);
    s.value4b(class_info.classFlags);
    s.text1b(class_info.subCategories);
    s.container2b(class_info.vendor);
    s.container2b(class_info.version);
    s.container2b(class_info.sdkVersion);
}

template <typename S>
void serialize(S& s, PFactoryInfo& factory_info) {
    s.text1b(factory_info.vendor);
    s.text1b(factory_info.url);
    s.text1b(factory_info.email);
    s.value4b(factory_info.flags);
}
}  // namespace Steinberg
