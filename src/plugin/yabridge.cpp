#include <vestige/aeffect.h>

#include <iostream>

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
 * The main VST plugin entry point. This finds the Windows VST plugin that
 * should be run, executes it in our VST host inside Wine, and sets up
 * communication between the two processes.
 */
VST_EXPORT AEffect* VSTPluginMain(audioMasterCallback /*audioMaster*/) {
    // TODO: Do something useful ehre
    std::cout << "Hello, world!" << std::endl;

    AEffect* effect = new AEffect();
    effect->magic = kEffectMagic;
    effect->dispatcher = dispatch;
    effect->process = process;
    // XXX: processReplacing?
    effect->setParameter = setParameter;
    effect->getParameter = getParameter;
    effect->numParams = 69;
    effect->uniqueID = 69420;

    return effect;
}

// TODO: I should track down the VST2 SDK for clarification on some of the
//       implementation details, such as the use of intptr_t isntead of void*
//       here.

/**
 * Handle an event sent by the VST host.
 *
 * TODO: Look up what the return value here is actually doing.
 */
intptr_t dispatch(AEffect* /*effect*/,
                  int32_t opcode,
                  int32_t /*parameter*/,
                  intptr_t /*value*/,
                  void* result,
                  float /*option*/) {
    switch (opcode) {
        case effGetEffectName:
            const std::string plugin_name("Hello, world!");
            std::copy(plugin_name.begin(), plugin_name.end(),
                      static_cast<char*>(result));

            return 1;  // TODO: Why?
            break;
    }

    // TODO: Unimplmemented
    return 0;
}

void process(AEffect*, float**, float**, int32_t) {
    // TODO: Unimplmemented
}

void setParameter(AEffect*, int32_t, float) {
    // TODO: Unimplmemented
}

float getParameter(AEffect*, int32_t) {
    // TODO: Unimplmemented
    return 0.0f;
}
