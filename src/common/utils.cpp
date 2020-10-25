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

#include "utils.h"

#include <sched.h>
#include <boost/process/environment.hpp>

namespace bp = boost::process;
namespace fs = boost::filesystem;

fs::path get_temporary_directory() {
    bp::environment env = boost::this_process::environment();
    if (!env["XDG_RUNTIME_DIR"].empty()) {
        return env["XDG_RUNTIME_DIR"].to_string();
    } else {
        return fs::temp_directory_path();
    }
}

bool set_realtime_priority() {
    sched_param params{.sched_priority = 5};
    return sched_setscheduler(0, SCHED_FIFO, &params) == 0;
}
