#include "bridge.h"

#include <iostream>
#include <msgpack.hpp>

#include "../common/events.h"

// TODO: I should track down the VST2 SDK for clarification on some of the
//       implementation details, such as the use of intptr_t isntead of void*
//       here.

/**
 * Handle an event sent by the VST host. Most of these opcodes will be passed
 * through to the winelib VST host.
 */
intptr_t Bridge::dispatch(AEffect* /*plugin*/,
                          int32_t opcode,
                          int32_t parameter,
                          intptr_t value,
                          void* result,
                          float option) {
    // TODO: Send to the Wine process
    Event event{opcode, parameter, value, option};
    msgpack::sbuffer buffer;
    msgpack::pack(buffer, event);

    // TODO: Read the response
    EventResult response;
    if (response.result) {
        std::copy(response.result->begin(), response.result->end(),
                  static_cast<char*>(result));
    }

    // Some events need some extra handling
    // TODO: Handle other things such as GUI itneraction
    switch (opcode) {
        case effClose:
            // TODO: Gracefully close the editor?

            // The VST API does not have an explicit function for releasing
            // resources, so we'll have to do it here. The actual plugin
            // instance gets freed by the host, or at least I think it does.
            delete this;

            return 0;
            break;
    }

    return response.return_value;
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
