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

#include <atomic>

#include <clap/entry.h>

/**
 * The number of active instances. Incremented when `clap_entry_init()` is
 * called, decremented when `clap_entry_exit()` is called. We'll initialize the
 * bridge when this is first incremented from 0, and we'll free the bridge again
 * when a `clap_entry_exit()` call causes this to return back to 0.
 */
std::atomic_size_t active_instances = 0;

bool clap_entry_init(const char* plugin_path) {
    // This function can be called multiple times, so we should make sure to
    // only initialize the bridge on the first call
    if (active_instances.fetch_add(1, std::memory_order_seq_cst) == 0) {
        // TODO: Increase reference count, init factory if it was zero
    }

    return true;
}

void clap_entry_deinit() {
    // We'll free the bridge when this exits brings the reference count back to
    // zero
    if (active_instances.fetch_sub(1, std::memory_order_seq_cst) == 1) {
        // TODO: Destroy the bridge instance
    }
}

const void* clap_entry_get_factory(const char* factory_id) {
    // TODO: Do the thing
    // TODO: Assertion
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init = clap_entry_init,
    .deinit = clap_entry_deinit,
    .get_factory = clap_entry_get_factory,
};
