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

#include <thread>

#include "../../common/communication/vst3.h"
#include "../../common/logging/vst3.h"
#include "common.h"

/**
 * This handles the communication between the native host and a VST3 plugin
 * hosted in our Wine plugin host. VST3 is handled very differently from VST2
 * because a plugin is no longer its own entity, but rather a definition of
 * objects that the host can create and interconnect. This `Vst3PluginBridge`
 * will be instantiated when the plugin first gets loaded, and it will survive
 * until the last instance of the plugin gets removed. The Wine host process
 * will thus also have the same lifetime, and even with yabridge's 'individual'
 * plugin hosting other instances of the same plugin will be handled by a single
 * process.
 *
 * @remark See the comments at the top of `vst3-plugin.cpp` for more
 *   information.
 *
 * The naming scheme of all of these 'bridge' classes is `<type>{,Plugin}Bridge`
 * for greppability reasons. The `Plugin` infix is added on the native plugin
 * side.
 */
class Vst3PluginBridge : PluginBridge<Vst3Sockets<std::jthread>> {
   public:
    /**
     * Initializes the VST3 module by starting and setting up communicating with
     * the Wine plugin host.
     *
     * @throw std::runtime_error Thrown when the Wine plugin host could not be
     *   found, or if it could not locate and load a VST3 module.
     */
    Vst3PluginBridge();

   private:
    /**
     * The logging facility used for this instance of yabridge. Wraps around
     * `PluginBridge::generic_logger`.
     */
    Vst3Logger logger;
};
