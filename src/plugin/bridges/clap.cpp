// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2022 Robbert van der Helm
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

#include "clap.h"

namespace fs = ghc::filesystem;

ClapPluginBridge::ClapPluginBridge(const ghc::filesystem::path& plugin_path)
    : PluginBridge(
          PluginType::clap,
          plugin_path,
          [](asio::io_context& io_context, const PluginInfo& info) {
              return ClapSockets<std::jthread>(
                  io_context,
                  generate_endpoint_base(info.native_library_path_.filename()
                                             .replace_extension("")
                                             .string()),
                  true);
          }),
      logger_(generic_logger_) {
    log_init_message();

    // This will block until all sockets have been connected to by the Wine VST
    // host
    connect_sockets_guarded();

    // Now that communication is set up the Wine host can send callbacks to this
    // bridge class, and we can send control messages to the Wine host. This
    // messaging mechanism is how we relay the CLAP communication protocol. As a
    // first thing, the Wine plugin host will ask us for a copy of the
    // configuration.
    host_callback_handler_ = std::jthread([&]() {
        set_realtime_priority(true);
        pthread_setname_np(pthread_self(), "host-callbacks");

        // TODO: Add the rest of the callbacks
        sockets_.plugin_host_main_thread_callback_.receive_messages(
            std::pair<ClapLogger&, bool>(logger_, false),
            overload{
                [&](const WantsConfiguration& request)
                    -> WantsConfiguration::Response {
                    warn_on_version_mismatch(request.host_version);

                    return config_;
                },
            });
    });
}

ClapPluginBridge::~ClapPluginBridge() noexcept {
    try {
        // Drop all work make sure all sockets are closed
        plugin_host_->terminate();
        io_context_.stop();
    } catch (const std::system_error&) {
        // It could be that the sockets have already been closed or that the
        // process has already exited (at which point we probably won't be
        // executing this, but maybe if all the stars align)
    }
}

const void* ClapPluginBridge::get_factory(const char* factory_id) {
    // FIXME: Implement
    return nullptr;
}
