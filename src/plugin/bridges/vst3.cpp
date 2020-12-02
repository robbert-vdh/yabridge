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

#include "vst3.h"

#include "../../common/utils.h"

// TODO: Do all of the initialization stuff from `Vst2PluginBridge`
Vst3PluginBridge::Vst3PluginBridge()
    :  // TODO: This is technically correct because we can configure the entire
       // directory at once
      config(load_config_for(get_this_file_location())),
      // TODO: This is incorrect for VST3 modules
      plugin_module_path(find_vst_plugin()),
      io_context(),
      sockets(io_context,
              // TODO: This is incorrect
              generate_endpoint_base(
                  plugin_module_path.filename().replace_extension("").string()),
              true),
      logger(Logger::create_from_environment(
          create_logger_prefix(sockets.base_dir))),
      wine_version(get_wine_version()),
      vst_host(
          config.group
              ? std::unique_ptr<HostProcess>(std::make_unique<GroupHost>(
                    io_context,
                    logger,
                    HostRequest{.plugin_type = PluginType::vst2,
                                .plugin_path = plugin_module_path.string(),
                                .endpoint_base_dir = sockets.base_dir.string()},
                    sockets,
                    *config.group))
              : std::unique_ptr<HostProcess>(std::make_unique<IndividualHost>(
                    io_context,
                    logger,
                    HostRequest{
                        .plugin_type = PluginType::vst2,
                        .plugin_path = plugin_module_path.string(),
                        .endpoint_base_dir = sockets.base_dir.string()}))),
      has_realtime_priority(set_realtime_priority()),
      wine_io_handler([&]() { io_context.run(); }) {
    log_init_message();
}

void Vst3PluginBridge::log_init_message() {
    // TODO: Move `Vst2PluginBridge::log_init_message()` to utils and call that
}
