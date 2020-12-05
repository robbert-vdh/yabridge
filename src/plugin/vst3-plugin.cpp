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

#include <public.sdk/source/main/pluginfactory.h>

#include "bridges/vst3.h"
// TODO: Remove include, instantiating and returning the `YaPluginFactory`
//       should be done in `Vst3PluginBridge`
#include "src/plugin/bridges/vst3-impls.h"

#include <public.sdk/source/main/linuxmain.cpp>

using Steinberg::gPluginFactory;

// Because VST3 plugins consist of completely independent components that have
// to be initialized and connected by the host, hosting a VST3 plugin through
// yabridge works very differently from hosting VST2 plugin. Even with
// individually hosted plugins, all instances of the plugin will be handled by a
// single dynamic library (that VST3 calls a 'module'). Because of this, we'll
// spawn our host process when the first instance of a plugin gets initialized,
// and when the last instance exits so will the host process.
//
// Even though the new VST3 module format where everything's inside of a bundle
// is not particularly common, it is the only standard for Linux and that's what
// we'll use. The installation format for yabridge will thus have the Windows
// plugin symlinked to either the `x86_64-win` or the `x86-win` directory inside
// of the bundle, even if it does not come in a bundle itself.

Vst3PluginBridge* bridge = nullptr;

bool InitModule() {
    assert(bridge == nullptr);

    try {
        // This is the only place where we have to use manual memory management.
        // The bridge's destructor is called when the `effClose` opcode is
        // received.
        bridge = new Vst3PluginBridge();

        return true;
    } catch (const std::exception& error) {
        Logger logger = Logger::create_from_environment();
        logger.log("Error during initialization:");
        logger.log(error.what());

        return false;
    }
}

bool DeinitModule() {
    assert(bridge != nullptr);

    delete bridge;
    return true;
}

/**
 * Our VST3 plugin's entry point. When building the plugin factory we'll host
 * the plugin in our Wine application, retrieve its information and supported
 * classes, and then recreate it here.
 */
SMTG_EXPORT_SYMBOL Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory() {
    // The host should have called `InitModule()` first
    assert(bridge);

    // TODO: Instead of using gPluginFactory we'll use a field in
    //       `Vst3PluginBridge`
    // TODO: First thing we should do is query the factory on the Wine side and
    //       preset a copy of it to the host. The important bits there are that
    //       we use the same interface version as the one presented the plugin.
    // TODO: We have two options for the implementation:
    //       1. We can query the interface version, and then have three
    //          different implementations for the interface version.
    //       2. We can implement version 3, but copy the iid from the plugin so
    //          it always uses the correct version.

    // TODO: Remove, this is just for type checking
    if (false) {
        boost::asio::local::stream_protocol::socket* socket;
        YaPluginFactoryPluginImpl object(*bridge);
        write_object(*socket, object);
    }

    if (!gPluginFactory) {
        // TODO: Here we want to:
        //       1) Load the plugin on the Wine host
        //       2) Create a factory using the plugins PFactoryInfo
        //       3) Get all PClassInfo{,2,W} objects from the plugin, register
        //          those classes.
        //
        //       We should wrap this in our `Vst3PluginBridge`
        // TODO: We should also create a list of which extensions we have
        //       already implemented and which are left
        // TODO: And when we get a query for some interface that we do not (yet)
        //       support, we should print some easy to spot warning message
        // TODO: Check whether `IPlugView::isPlatformTypeSupported` needs
        //       special handling.
        // TODO: Should we always use plugin groups or for VST3 plugins? Since
        //       they seem to be very keen on sharing resources and leaving
        //       modules loaded.
        // TODO: The documentation mentions that private communication through
        //       VST3's message system should be handled on a separate timer
        //       thread.  Do we need special handling for this on the Wine side
        //       (e.g. during the event handling loop)? Probably not, since the
        //       actual host should manage all messaging.
        // TODO: The docs very explicitly mention that
        //       the`IComponentHandler::{begin,perform,end}Edit()` functions
        //       have to be called from the UI thread. Should we have special
        //       handling for this or does everything just magically work out?
        // TODO: Something that's not relevant here but that will require some
        //       thinking is that VST3 requires all plugins to be installed in
        //       ~/.vst3. I can think of two options and I"m not sure what's the
        //       best one:
        //
        //       1. We can add the required files for the Linux VST3 plugin to
        //          the location of the Windows VST3 plugin (by adding some
        //          files to the  bundle or creating a bundle next to it) and
        //          then symlink that bundle to ~/.vst3.
        //       2. We can create the bundle in ~/.vst3 and symlink the Windows
        //          plugin and all of its resources into bundle as if they were
        //          also installed there.
        //
        //       The second one sounds much better, but it will still need some
        //       more consideration. Aside from that VST3 plugins also have a
        //       centralized preset location, even though barely anyone uses it,
        //       yabridgectl will also have to make a symlink of that. Also,
        //       yabridgectl will need to do some extra work there to detect
        //       removed plugins.
        // TODO: Also symlink presets, and allow pruning broken symlinks there
        //       as well
        // TODO: And how do we choose between 32-bit and 64-bit versions of a
        //        VST3 plugin if they exist? Config files?

        // static Steinberg::PFactoryInfo factoryInfo(vendor, url, email,
        // flags); gPluginFactory = new Steinberg::CPluginFactory(factoryInfo);

        //
        // {
        //     Steinberg::TUID lcid = cid;
        //     static Steinberg::PClassInfo componentClass(lcid, cardinality,
        //                                                 category, name);
        //     gPluginFactory->registerClass(&componentClass, createMethod);
        // }
        // {
        //     Steinberg::TUID lcid = cid;
        //     static Steinberg::PClassInfo2 componentClass(
        //         lcid, cardinality, category, name, classFlags, subCategories,
        //         0, version, sdkVersion);
        //     gPluginFactory->registerClass(&componentClass, createMethod);
        // }
        // {
        //     TUID lcid = cid;
        //     static Steinberg::PClassInfoW componentClass(
        //         lcid, cardinality, category, name, classFlags, subCategories,
        //         0, version, sdkVersion);
        //     gPluginFactory->registerClass(&componentClass, createMethod);
        // }
    } else {
        gPluginFactory->addRef();
    }

    return gPluginFactory;
}
