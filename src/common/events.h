#pragma once

#include <cinttypes>
#include <msgpack.hpp>
#include <optional>

/**
 * An event as dispatched by the VST host. These events will get forwarded to
 * the VST host process running under Wine. The fields here mirror those
 * arguments sent to the `AEffect::dispatch` function.
 */
struct Event {
    int32_t opcode;
    int32_t parameter;
    // TODO: This is an intptr_t, is this actually a poitner that should be
    //       dereferenced?
    intptr_t value;
    float option;

    MSGPACK_DEFINE(opcode, parameter, value, option)
};

/**
 * AN instance of this should be sent back as a response to an incoming event.
 */
struct EventResult {
    /**
     * The result that should be returned from the dispatch function.
     */
    intptr_t return_value;
    /**
     * If present, this should get written into the void pointer passed to the
     * dispatch function.
     */
    std::optional<std::string> result;

    // TODO: Add missing return value fields;

    MSGPACK_DEFINE(return_value, result)
};
