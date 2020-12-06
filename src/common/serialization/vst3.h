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

#include <variant>

#include <bitsery/ext/std_variant.h>

#include "../configuration.h"
#include "../utils.h"
#include "common.h"
#include "vst3/plugin-factory.h"

// Event handling for our VST3 plugins works slightly different from how we
// handle VST2 plugins. VST3 does not have a centralized event dispatching
// interface like VST2 does, and it uses a bunch of separate interfaces instead.
// Instead of having an a single event/result with accompanying payload values
// for both host -> plugin `dispatcher()` and plugin -> host `audioMaster()`
// calls, we'll just send request and response payloads directly without any
// metadata. We also split everything up into host -> plugin 'control' payloads
// and plugin -> host 'callback' payloads for maintainability's sake.

// TODO: If this approach works, maybe we can also refactor the VST2 handling to
//       do this since it's a bit safer and easier to read

/**
 * Marker struct to indicate the other side (the plugin) should send a copy of
 * the configuration.
 */
struct WantsConfiguration {
    using Response = Configuration;
};

/**
 * When we send a control message from the plugin to the Wine VST host, this
 * encodes the information we request or the operation we want to perform. A
 * request of type `ControlRequest(T)` should send back a `T::Reponse`.
 */
using ControlRequest = std::variant<>;

template <typename S>
void serialize(S& s, ControlRequest& payload) {
    s.ext(payload, bitsery::ext::StdVariant{});
}

/**
 * When we do a callback from the Wine VST host to the plugin, this encodes the
 * information we want or the operation we want to perform. A request of type
 * `CallbackRequest(T)` should send back a `T::Reponse`.
 */
using CallbackRequest = std::variant<WantsConfiguration>;

template <typename S>
void serialize(S& s, CallbackRequest& payload) {
    s.ext(payload, bitsery::ext::StdVariant{[](S&, WantsConfiguration&) {}});
}
