#include <public.sdk/source/main/pluginfactory.h>

// TODO: Should you include this implementation file or copy everything over?
#include <public.sdk/source/main/linuxmain.cpp>

using Steinberg::gPluginFactory;

// TODO: What should we do here? Also, note to self, don't forget to call these
//       on the Wine host side if the host SDK doesn't already do that for us.
bool InitModule() {
    return true;
}

bool DeinitModule() {
    return true;
}

/**
 * Our VST3 plugin's entry point. When building the plugin factory we'll host
 * the plugin in our Wine application, retrieve its information and supported
 * classes, and then recreate it here.
 */
SMTG_EXPORT_SYMBOL Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory() {
    // TODO: So from this I can imagine that the host is supposed to keep this
    //       module loaded into memory and reuse it for multiple plugins? How
    //       should Wine host instances be tied to native plugin instances?
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
        // TODO: The documentation mentions that private communication through
        //       VST3's message system should be handled on a separate timer
        //       thread.  Do we need special handling for this on the Wine side
        //       (e.g. during the event handling loop)? Probably not, since the
        //       actual host should manage all messaging.
        // TODO: The docs very explicitly mention that
        //       the`IComponentHandler::{begin,perform,end}Edit()` functions
        //       have to be called from the UI thread. Should we have special
        //       handling for this or does everything just magically work out?

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
