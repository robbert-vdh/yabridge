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

#pragma once

#include <filesystem>
#include <optional>

/**
 * Starting from the starting file or directory, go up in the directory
 * hierarchy until we find a file named `filename`.
 *
 * @param filename The name of the file we're looking for. This can also be a
 *   directory name since directories are also files.
 * @param starting_from The directory to start searching in. If this is a file,
 *   then start searching in the directory the file is located in.
 * @param predicate The predicate to use to check if the path matches a file.
 *   Needed as an easy way to limit the search to directories only since C++17
 *   does not have any built in coroutines or generators.
 *
 * @return The path to the *file* found, or `std::nullopt` if the file could not
 *   be found.
 */
template <typename F = bool(const std::filesystem::path&)>
std::optional<std::filesystem::path> find_dominating_file(
    const std::string& filename,
    std::filesystem::path starting_dir,
    F predicate = std::filesystem::exists) {
    while (starting_dir != "") {
        const std::filesystem::path candidate = starting_dir / filename;
        if (predicate(candidate)) {
            return candidate;
        }

        starting_dir = starting_dir.parent_path();
    }

    return std::nullopt;
}
