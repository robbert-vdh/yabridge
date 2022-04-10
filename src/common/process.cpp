// yabridge: a Wine VST bridge
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

#include "process.h"

#include <cassert>

ProcessEnvironment::ProcessEnvironment(char** initial_env) {
    // We'll need to read all strings from `initial_env`. They _should_ all be
    // zero-terminated strings, with a null pointer to indicate the end of the
    // array.
    assert(initial_env);
    while (*initial_env) {
        variables_.push_back(*initial_env);
        initial_env++;
    }
}

bool ProcessEnvironment::contains(const std::string_view& key) const {
    for (const auto& variable : variables_) {
        if (variable.starts_with(key) && variable.size() > key.size() &&
            variable[key.size()] == '=') {
            return true;
        }
    }

    return false;
}

std::optional<std::string_view> ProcessEnvironment::get(
    const std::string_view& key) const {
    for (const auto& variable : variables_) {
        if (variable.starts_with(key) && variable.size() > key.size() &&
            variable[key.size()] == '=') {
            return std::string_view(variable).substr(key.size() + 1);
        }
    }

    return std::nullopt;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void ProcessEnvironment::insert(const std::string& key,
                                const std::string& value) {
    variables_.push_back(key + "=" + value);
}

char* const* ProcessEnvironment::make_environ() const {
    recreated_environ_.clear();

    for (const auto& variable : variables_) {
        recreated_environ_.push_back(variable.c_str());
    }
    recreated_environ_.push_back(nullptr);

    return const_cast<char* const*>(recreated_environ_.data());
}
