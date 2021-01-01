// yabridge: a Wine VST bridge
// Copyright (C) 2020-2021 Robbert van der Helm
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

#include "vst2.h"

EventPayload DefaultDataConverter::read(const int /*opcode*/,
                                        const int /*index*/,
                                        const intptr_t /*value*/,
                                        const void* data) const {
    if (!data) {
        return nullptr;
    }

    // This is a simple fallback that will work in almost every case.
    // Because some plugins don't zero out their string buffers when sending
    // host callbacks, we will explicitely list all callbacks that expect a
    // string in `DispatchDataConverter` adn `HostCallbackDataConverter`.
    const char* c_string = static_cast<const char*>(data);
    if (c_string[0] != 0) {
        return std::string(c_string);
    } else {
        return WantsString{};
    }
}

std::optional<EventPayload> DefaultDataConverter::read_value(
    const int /*opcode*/,
    const intptr_t /*value*/) const {
    return std::nullopt;
}

void DefaultDataConverter::write(const int /*opcode*/,
                                 void* data,
                                 const EventResult& response) const {
    // The default behavior is to handle this as a null terminated C-style
    // string
    std::visit(overload{[&](const auto&) {},
                        [&](const std::string& s) {
                            char* output = static_cast<char*>(data);

                            // We use std::string for easy transport but in
                            // practice we're always writing null terminated
                            // C-style strings
                            std::copy(s.begin(), s.end(), output);
                            output[s.size()] = 0;
                        }},
               response.payload);
}

void DefaultDataConverter::write_value(const int /*opcode*/,
                                       intptr_t /*value*/,
                                       const EventResult& /*response*/) const {}

intptr_t DefaultDataConverter::return_value(const int /*opcode*/,
                                            const intptr_t original) const {
    return original;
}
