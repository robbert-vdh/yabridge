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
