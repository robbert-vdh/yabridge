#include <vestige/aeffect.h>

extern "C" {
// TODO: Remove
#include <cstdio>

#define VST_EXPORT __attribute__((visibility("default")))

/**
 * The main VST plugin entry point. All other functions exported below are
 * fallbacks for old VST hosts.
 */
VST_EXPORT AEffect* VSTPluginMain(audioMasterCallback audioMaster) {
    // TODO
    printf("Hello, world!\n");
    return nullptr;
}

VST_EXPORT AEffect* MAIN(audioMasterCallback audioMaster) {
    return VSTPluginMain(audioMaster);
}

VST_EXPORT AEffect* main_plugin(audioMasterCallback audioMaster) {
    return VSTPluginMain(audioMaster);
}
}
