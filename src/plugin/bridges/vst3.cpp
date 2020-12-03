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

Vst3PluginBridge::Vst3PluginBridge()
    : PluginBridge(PluginType::vst3,
                   // TODO: This is incorrect for VST3 modules
                   find_vst_plugin(),
                   [](boost::asio::io_context& io_context) {
                       return Vst3Sockets<std::jthread>(
                           io_context,
                           generate_endpoint_base(find_vst_plugin()
                                                      .filename()
                                                      .replace_extension("")
                                                      .string()),
                           true);
                   }),
      // TODO: This is UB, use composition with `generic_logger` instead
      logger(static_cast<Vst3Logger&&>(Logger::create_from_environment(
          create_logger_prefix(sockets.base_dir)))) {
    log_init_message();

    // TODO: Call the host guard handler
}
