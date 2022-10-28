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

#pragma once

// This header is used by the plugins and the chainloaders to send desktop
// notifications when something goes wrong

#include <ghc/filesystem.hpp>
#include <optional>

// TODO: At some point, provide an alternative to notify-send by dlopen()-ing
//       libdbus instead. Some more obscure distros won't have notify-send
//       available.

/**
 * Send a desktop notification using `notify-send`. Used for diagnostics when a
 * plugin fails to load since the user may not be checking the output in a
 * terminal.
 *
 * @param title The title (or technically, summary) of the notification.
 * @param body The message to display. This can contain line feeds, and it any
 *   HTML tags and XML escape sequences will be automatically escaped. The
 *   message can also be empty.
 * @param origin If this is set to the current plugin's path, then the
 *   notification will append a 'Source: <XXX.so>' hyperlink to the body so the
 *   user can more easily navigate to the plugin's path.
 *
 * @return Whether the notification was sent. This will be false if
 *   `notify-send` is not available.
 */
bool send_notification(const std::string& title,
                       const std::string body,
                       std::optional<ghc::filesystem::path> origin);
