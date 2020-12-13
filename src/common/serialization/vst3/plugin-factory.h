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

#include <set>

#include <bitsery/ext/std_optional.h>
#include <bitsery/ext/std_set.h>
#include <bitsery/traits/string.h>
#include <pluginterfaces/base/ipluginbase.h>

#include "../../bitsery/ext/vst3.h"
#include "base.h"
#include "host-application.h"

// TODO: After implementing one or two more of these, abstract away some of the
//       nasty bits

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IPluginFactory{1,2,3}` for serialization purposes. See
 * `README.md` for more information on how this works.
 */
class YaPluginFactory : public Steinberg::IPluginFactory3 {
   public:
    /**
     * These are the arguments for creating a `YaPluginFactoryPluginImpl`.
     */
    struct ConstructArgs {
        ConstructArgs();

        /**
         * Create a copy of an existing plugin factory. Depending on the
         * supported interface function more or less of this struct will be left
         * empty, and `iid` will be set accordingly.
         */
        ConstructArgs(Steinberg::IPtr<Steinberg::IPluginFactory> factory);

        /**
         * The IIDs that the interface we serialized supports.
         */
        std::set<Steinberg::FUID> known_iids;

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
            s.ext(known_iids, bitsery::ext::StdSet{32},
                  [](S& s, Steinberg::FUID& iid) {
                      s.ext(iid, bitsery::ext::FUID{});
                  });
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
     * Message to request the `IPluginFactory{,2,3}`'s information from the Wine
     * plugin host.
     */
    struct Construct {
        using Response = ConstructArgs;

        template <typename S>
        void serialize(S&) {}
    };

    /**
     * Instantiate this instance with arguments read from the Windows VST3
     * plugin's plugin factory.
     */
    YaPluginFactory(const ConstructArgs&& args);

    /**
     * We do not need to implement the destructor in
     * `YaPluginFactoryPluginImpl`, since when the sockets are closed, RAII will
     * clean up the Windows VST3 module we loaded along with its factory for us.
     */
    virtual ~YaPluginFactory();

    DECLARE_FUNKNOWN_METHODS

    // From `IPluginFactory`
    tresult PLUGIN_API getFactoryInfo(Steinberg::PFactoryInfo* info) override;
    int32 PLUGIN_API countClasses() override;
    tresult PLUGIN_API getClassInfo(Steinberg::int32 index,
                                    Steinberg::PClassInfo* info) override;
    /**
     * See the implementation in `YaPluginFactoryPluginImpl` for how this is
     * handled.
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
     * the Wine plugin host. A proxy `YaHostApplication` should be created on
     * the Wine plugin host and then passed as an argument to
     * `IPluginFactory3::setHostContext()`. If the host called
     * `IPluginFactory3::setHostContext()` with something other than an
     * `IHostApplication*`, we return an error immediately and log the call.
     */
    struct SetHostContext {
        using Response = UniversalTResult;

        YaHostApplication::ConstructArgs host_application_context_args;

        template <typename S>
        void serialize(S& s) {
            s.object(host_application_context_args);
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
    s.text2b(class_info.name);
    s.value4b(class_info.classFlags);
    s.text1b(class_info.subCategories);
    s.text2b(class_info.vendor);
    s.text2b(class_info.version);
    s.text2b(class_info.sdkVersion);
}

template <typename S>
void serialize(S& s, PFactoryInfo& factory_info) {
    s.text1b(factory_info.vendor);
    s.text1b(factory_info.url);
    s.text1b(factory_info.email);
    s.value4b(factory_info.flags);
}
}  // namespace Steinberg
