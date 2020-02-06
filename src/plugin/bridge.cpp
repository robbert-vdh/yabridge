#include "bridge.h"

#include <iostream>

// TODO: I should track down the VST2 SDK for clarification on some of the
//       implementation details, such as the use of intptr_t isntead of void*
//       here.

/**
 * Handle an event sent by the VST host. Most of these opcodes will be passed
 * through to the winelib VST host.
 */
intptr_t Bridge::dispatch(AEffect* /*plugin*/,
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

            // return 1;  // TODO: Why?
            break;
    }

    // TODO: Unimplmemented
    return 0;
}

void Bridge::process(AEffect* /*plugin*/,
                     float** /*inputs*/,
                     float** /*outputs*/,
                     int32_t /*sample_frames*/) {
    // TODO: Unimplmemented
}

void Bridge::set_parameter(AEffect* /*plugin*/,
                           int32_t /*index*/,
                           float /*value*/) {
    // TODO: Unimplmemented
}

float Bridge::get_parameter(AEffect* /*plugin*/, int32_t /*index*/
) {
    // TODO: Unimplmemented
    return 0.0f;
}
