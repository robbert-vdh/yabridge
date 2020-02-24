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

#include <vestige/aeffect.h>

#include <iostream>
#include <memory>

#include "bridge.h"

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
 * Fetch the bridge instance stored in an unused pointer from a VST plugin. This
 * is sadly needed as a workaround to avoid using globals since we need free
 * function pointers to interface with the VST C API.
 */
Bridge& get_bridge_instance(const AEffect& plugin) {
    return *static_cast<Bridge*>(plugin.ptr3);
}

/**
 * The main VST plugin entry point. We first set up a bridge that connects to a
 * Wine process that hosts the Windows VST plugin. We then create and return a
 * VST plugin struct that acts as a passthrough to the bridge.
 *
 * To keep this somewhat contained this is the only place where we're doing
 * manual memory management. Clean up is done when we receive the `effClose`
 * opcode from the VST host (i.e. opcode 1).`
 */
VST_EXPORT AEffect* VSTPluginMain(audioMasterCallback /*audioMaster*/) {
    try {
        Bridge* bridge = new Bridge();

        AEffect* plugin = new AEffect();
        plugin->ptr3 = bridge;

        plugin->dispatcher = dispatch;
        plugin->process = process;
        plugin->setParameter = setParameter;
        plugin->getParameter = getParameter;
        // // XXX: processReplacing?

        // TODO: Add more and actual data
        plugin->magic = kEffectMagic;
        plugin->numParams = 69;
        plugin->uniqueID = 69420;

        return plugin;
    } catch (const std::exception& error) {
        std::cerr << "Error during initialization:" << std::endl;
        std::cerr << error.what() << std::endl;

        return nullptr;
    }
}

// The below functions are proxy functions for the methods defined in
// `Bridge.cpp`

intptr_t dispatch(AEffect* plugin,
                  int32_t opcode,
                  int32_t parameter,
                  intptr_t value,
                  void* data,
                  float option) {
    return get_bridge_instance(*plugin).dispatch(plugin, opcode, parameter,
                                                 value, data, option);
}

void process(AEffect* plugin,
             float** inputs,
             float** outputs,
             int32_t sample_frames) {
    return get_bridge_instance(*plugin).process(plugin, inputs, outputs,
                                                sample_frames);
}

void setParameter(AEffect* plugin, int32_t index, float value) {
    return get_bridge_instance(*plugin).set_parameter(plugin, index, value);
}

float getParameter(AEffect* plugin, int32_t index) {
    return get_bridge_instance(*plugin).get_parameter(plugin, index);
}
