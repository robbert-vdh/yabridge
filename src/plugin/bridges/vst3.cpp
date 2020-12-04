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
    : PluginBridge(
          PluginType::vst3,
          [](boost::asio::io_context& io_context, const PluginInfo& info) {
              return Vst3Sockets<std::jthread>(
                  io_context,
                  generate_endpoint_base(info.native_library_path.filename()
                                             .replace_extension("")
                                             .string()),
                  true);
          }),
      logger(generic_logger) {
    log_init_message();

    // This will block until all sockets have been connected to by the Wine VST
    // host
    connect_sockets_guarded();

    // Now that communication is set up the Wine host can send callbacks to this
    // bridge class, and we can send control messages to the Wine host. This
    // messaging mechanism is how we relay the VST3 communication protocol. As a
    // first thing, the Wine VST host will ask us for a copy of the
    // configuration.
    host_callback_handler = std::jthread([&]() {
        sockets.vst_host_callback.receive_messages(
            std::pair<Vst3Logger&, bool>(logger, false),
            [&](CallbackRequest request) -> CallbackResponse {
                return std::visit(overload{[&](const WantsConfiguration&)
                                               -> WantsConfiguration::Response {
                                      return config;
                                  }},
                                  request);
            });
    });
}
