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
#include "vst3/component-handler-proxy.h"
#include "vst3/host-context-proxy.h"
#include "vst3/plugin-factory.h"
#include "vst3/plugin-proxy.h"

// Event handling for our VST3 plugins works slightly different from how we
// handle VST2 plugins. VST3 does not have a centralized event dispatching
// interface like VST2 does, and it uses a bunch of separate interfaces instead.
// Instead of having an a single event/result with accompanying payload values
// for both host -> plugin `dispatcher()` and plugin -> host `audioMaster()`
// calls, we'll send objects of type `T` that should receive a response of type
// `T::Response`, where all of the possible `T`s are stored in an
// `std::variant`. This way we communicate in a completely type safe way.

// TODO: If this approach works, maybe we can also refactor the VST2 handling to
//       do this since it's a bit safer and easier to read

// All messages for creating objects and calling interfaces on them are defined
// as part of the interfaces and implementations in `vst3/`

/**
 * Marker struct to indicate the other side (the plugin) should send a copy of
 * the configuration.
 */
struct WantsConfiguration {
    using Response = Configuration;

    template <typename S>
    void serialize(S&) {}
};

/**
 * When we send a control message from the plugin to the Wine VST host, this
 * encodes the information we request or the operation we want to perform. A
 * request of type `ControlRequest(T)` should send back a `T::Response`.
 */
using ControlRequest = std::variant<Vst3PluginProxy::Construct,
                                    Vst3PluginProxy::Destruct,
                                    Vst3PluginProxy::SetState,
                                    Vst3PluginProxy::GetState,
                                    YaAudioProcessor::SetBusArrangements,
                                    YaAudioProcessor::GetBusArrangement,
                                    YaAudioProcessor::CanProcessSampleSize,
                                    YaAudioProcessor::GetLatencySamples,
                                    YaAudioProcessor::SetupProcessing,
                                    YaAudioProcessor::SetProcessing,
                                    YaAudioProcessor::Process,
                                    YaAudioProcessor::GetTailSamples,
                                    YaComponent::GetControllerClassId,
                                    YaComponent::SetIoMode,
                                    YaComponent::GetBusCount,
                                    YaComponent::GetBusInfo,
                                    YaComponent::GetRoutingInfo,
                                    YaComponent::ActivateBus,
                                    YaComponent::SetActive,
                                    YaConnectionPoint::Connect,
                                    YaConnectionPoint::Disconnect,
                                    YaEditController::SetComponentState,
                                    YaEditController::GetParameterCount,
                                    YaEditController::GetParameterInfo,
                                    YaEditController::GetParamStringByValue,
                                    YaEditController::GetParamValueByString,
                                    YaEditController::NormalizedParamToPlain,
                                    YaEditController::PlainParamToNormalized,
                                    YaEditController::GetParamNormalized,
                                    YaEditController::SetParamNormalized,
                                    YaEditController::SetComponentHandler,
                                    YaPluginBase::Initialize,
                                    YaPluginBase::Terminate,
                                    YaPluginFactory::Construct,
                                    YaPluginFactory::SetHostContext>;

template <typename S>
void serialize(S& s, ControlRequest& payload) {
    // All of the objects in `ControlRequest` should have their own
    // serialization function.
    s.ext(payload, bitsery::ext::StdVariant{});
}

/**
 * When we do a callback from the Wine VST host to the plugin, this encodes the
 * information we want or the operation we want to perform. A request of type
 * `CallbackRequest(T)` should send back a `T::Response`.
 */
using CallbackRequest = std::variant<WantsConfiguration,
                                     YaComponentHandler::BeginEdit,
                                     YaComponentHandler::PerformEdit,
                                     YaComponentHandler::EndEdit,
                                     YaComponentHandler::RestartComponent,
                                     YaHostApplication::GetName>;

template <typename S>
void serialize(S& s, CallbackRequest& payload) {
    // All of the objects in `CallbackRequest` should have their own
    // serialization function.
    s.ext(payload, bitsery::ext::StdVariant{});
}
