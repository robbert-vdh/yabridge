#include <vestige/aeffect.h>

#include <iostream>
#include <memory>

#include "bridge.h"

#define VST_EXPORT __attribute__((visibility("default")))

// The main entry point for VST plugins should be called `VSTPluginMain``. The
// other two exist for legacy reasons since some old hosts might still use
// them.`
extern "C" {
extern VST_EXPORT AEffect* VSTPluginMain(audioMasterCallback);

VST_EXPORT AEffect* MAIN(audioMasterCallback audioMaster) {
    return VSTPluginMain(audioMaster);
}

VST_EXPORT AEffect* main_plugin(audioMasterCallback audioMaster) {
    return VSTPluginMain(audioMaster);
}
}

intptr_t dispatch(AEffect*, int32_t, int32_t, intptr_t, void*, float);
void process(AEffect*, float**, float**, int32_t);
void setParameter(AEffect*, int32_t, float);
float getParameter(AEffect*, int32_t);

/**
 * Retrieve the bridge instance stored in an unused pointer from a VST plugin.
 * This is sadly needed as a workaround to avoid using globals since we need
 * free function pointers to interface with the VST API.
 */
Bridge* get_bridge_instance(const AEffect& plugin) {
    return static_cast<Bridge*>(plugin.ptr3);
}

/**
 * The main VST plugin entry point. This finds the Windows VST plugin that
 * should be run, executes it in our VST host inside Wine, and sets up
 * communication between the two processes.
 *
 * This is a bit of a mess since we're interacting with an external C API. To
 * keep this somewhat contained this is the only place where we're doing manual
 * memory management.
 */
VST_EXPORT AEffect* VSTPluginMain(audioMasterCallback /*audioMaster*/) {
    // TODO: Since we are returning raw pointers, how does cleanup work?
    AEffect* plugin = new AEffect();
    plugin->ptr3 = new Bridge();

    plugin->dispatcher = dispatch;
    plugin->process = process;
    plugin->setParameter = setParameter;
    plugin->getParameter = getParameter;
    // // XXX: processReplacing?

    // TODO: Add more and actual data
    plugin->magic = kEffectMagic;
    plugin->numParams = 69;
    plugin->uniqueID = 69420;

    return plugin;
}

// The below functions are proxy functions for the methods defined in
// `Bridge.cpp`

intptr_t dispatch(AEffect* plugin,
                  int32_t opcode,
                  int32_t parameter,
                  intptr_t value,
                  void* result,
                  float option) {
    return get_bridge_instance(*plugin)->dispatch(plugin, opcode, parameter,
                                                  value, result, option);
}

void process(AEffect* plugin,
             float** inputs,
             float** outputs,
             int32_t sample_frames) {
    return get_bridge_instance(*plugin)->process(plugin, inputs, outputs,
                                                 sample_frames);
}

void setParameter(AEffect* plugin, int32_t index, float value) {
    return get_bridge_instance(*plugin)->set_parameter(plugin, index, value);
}

float getParameter(AEffect* plugin, int32_t index) {
    return get_bridge_instance(*plugin)->get_parameter(plugin, index);
}
