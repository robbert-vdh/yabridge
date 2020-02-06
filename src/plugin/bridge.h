#pragma once

#include <vestige/aeffect.h>

/**
 * This handles the communication between the Linux native VST plugin and the
 * Wine VST host. The functions below should be used as callback functions in an
 * `AEffect` object.
 */
class Bridge {
   public:
    // The below four functions are the handlers from the VST2 API. They are
    // called through proxy functions in `plugin.cpp`.

    /**
     * Handle an event sent by the VST host. Most of these opcodes will be
     * passed through to the winelib VST host through a Unix domain socket.
     */
    intptr_t dispatch(AEffect* plugin,
                      int32_t opcode,
                      int32_t parameter,
                      intptr_t value,
                      void* result,
                      float option);
    void process(AEffect* plugin,
                 float** inputs,
                 float** outputs,
                 int32_t sample_frames);
    void set_parameter(AEffect* plugin, int32_t index, float value);
    float get_parameter(AEffect* plugin, int32_t index);
};
