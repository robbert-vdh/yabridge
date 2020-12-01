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

#pragma once

#include "../serialization/vst2.h"
#include "common.h"

/**
 * Convert an event opcode to a human readable string for debugging purposes.
 * See `src/include/vestige/aeffectx.h` for a complete list of these opcodes.
 *
 * @param is_dispatch Whether to use opcodes for the `dispatch` function. Will
 *   use the names from the host callback function if set to false.
 * @param opcode The opcode of the event.
 *
 * @return Either the name from `aeffectx.h`, or a nullopt if it was not listed
 *   there.
 */
std::optional<std::string> opcode_to_string(bool is_dispatch, int opcode);

class Vst2Logger : public Logger {
   public:
    // The following functions are for logging specific events, they are only
    // enabled for verbosity levels higher than 1 (i.e. `Verbosity::events`)
    void log_get_parameter(int index);
    void log_get_parameter_response(float vlaue);
    void log_set_parameter(int index, float value);
    void log_set_parameter_response();
    // If `is_dispatch` is `true`, then use opcode names from the plugin's
    // dispatch function. Otherwise use names for the host callback function
    // opcodes.
    void log_event(bool is_dispatch,
                   int opcode,
                   int index,
                   intptr_t value,
                   const EventPayload& payload,
                   float option,
                   const std::optional<EventPayload>& value_payload);
    void log_event_response(
        bool is_dispatch,
        int opcode,
        intptr_t return_value,
        const EventResultPayload& payload,
        const std::optional<EventResultPayload>& value_payload);

   private:
    /**
     * Determine whether an event should be filtered based on the current
     * verbosity level.
     */
    bool should_filter_event(bool is_dispatch, int opcode) const;
};
