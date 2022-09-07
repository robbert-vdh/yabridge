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

#include "host-proxy.h"

#include "../../../common/serialization/clap/version.h"

clap_host_proxy::clap_host_proxy(ClapBridge& bridge,
                                 size_t owner_instance_id,
                                 clap::host::Host host_args)
    : bridge_(bridge),
      owner_instance_id_(owner_instance_id),
      host_args_(std::move(host_args)),
      host_vtable_(clap_host_t{
          .clap_version = clamp_clap_version(host_args_.clap_version),
          .host_data = this,
          .name = host_args_.name.c_str(),
          .vendor = host_args_.vendor ? host_args_.vendor->c_str() : nullptr,
          .url = host_args_.url ? host_args_.url->c_str() : nullptr,
          .version = host_args_.version.c_str(),
          .get_extension = host_get_extension,
          .request_restart = host_request_restart,
          .request_process = host_request_process,
          .request_callback = host_request_callback,
      }) {}

const void* CLAP_ABI
clap_host_proxy::host_get_extension(const struct clap_host* host,
                                    const char* extension_id) {
    // TODO: Implement
    return nullptr;
}

void CLAP_ABI
clap_host_proxy::host_request_restart(const struct clap_host* host) {
    // TODO: Implement
}

void CLAP_ABI
clap_host_proxy::host_request_process(const struct clap_host* host) {
    // TODO: Implement
}

void CLAP_ABI
clap_host_proxy::host_request_callback(const struct clap_host* host) {
    // TODO: Implement
}
