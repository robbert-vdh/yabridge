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

#include "bridges/vst3.h"

#include <public.sdk/source/main/linuxmain.cpp>

// Because VST3 plugins consist of completely independent components that have
// to be initialized and connected by the host, hosting a VST3 plugin through
// yabridge works very differently from hosting VST2 plugin. Even with
// individually hosted plugins, all instances of the plugin will be handled by a
// single dynamic library (that VST3 calls a 'module'). Because of this, we'll
// spawn our host process when the first instance of a plugin gets initialized,
// and when the last instance exits so will the host process.
//
// Even though the new VST3 module format where everything's inside of a bundle
// is not particularly common, it is the only standard for Linux and that's what
// we'll use. The installation format for yabridge will thus have the Windows
// plugin symlinked to either the `x86_64-win` or the `x86-win` directory inside
// of the bundle, even if it does not come in a bundle itself.

Vst3PluginBridge* bridge = nullptr;

bool InitModule() {
    assert(bridge == nullptr);

    try {
        bridge = new Vst3PluginBridge();

        return true;
    } catch (const std::exception& error) {
        Logger logger = Logger::create_from_environment();
        logger.log("Error during initialization:");
        logger.log(error.what());

        return false;
    }
}

bool DeinitModule() {
    assert(bridge != nullptr);

    delete bridge;
    bridge = nullptr;

    return true;
}

/**
 * Our VST3 plugin's entry point. When building the plugin factory we'll host
 * the plugin in our Wine application, retrieve its information and supported
 * classes, and then recreate it here.
 */
SMTG_EXPORT_SYMBOL Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory() {
    // The host should have called `InitModule()` first
    assert(bridge);

    return bridge->get_plugin_factory();
}
