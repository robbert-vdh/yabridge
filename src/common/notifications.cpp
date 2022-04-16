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

#include "notifications.h"

#include <sstream>

#include "process.h"
#include "utils.h"

bool send_notification(const std::string& title,
                       const std::string body,
                       std::optional<ghc::filesystem::path> origin) {
    // I think there's a zero chance that we're going to call this function with
    // anything that even somewhat resembles HTML, but we should still do a
    // basic XML escape anyways.
    std::ostringstream formatted_body;
    formatted_body << xml_escape(body);

    // If the path to the current library file is provided, then we'll append
    // the path to that library file to the message. In earlier versions we
    // would detect the library path right here, but that will not work with
    // chainloaded plugins as they will load the actual plugin libraries from
    // fixed locations.
    if (origin) {
        try {
            formatted_body << "\n"
                           << "Source: <a href=\"file://"
                           << url_encode_path(origin->parent_path().string())
                           << "\">" << xml_escape(origin->filename().string())
                           << "</a>";
        } catch (const std::system_error&) {
            // I don't think this can fail in the way we're using it, but the
            // last thing we want is our notification informing the user of an
            // exception to trigger another exception
        }
    }

    Process process("notify-send");
    process.arg("--urgency=normal");
    process.arg("--app-name=yabridge");
    process.arg(title);
    process.arg(formatted_body.str());

    // We will have printed the message to the terminal anyways, so if the user
    // doesn't have libnotify installed we'll just fail silently
    const auto result = process.spawn_get_status();
    return std::visit(
        overload{
            [](int status) -> bool { return status == EXIT_SUCCESS; },
            [](const Process::CommandNotFound&) -> bool { return false; },
            [](const std::error_code&) -> bool { return false; },
        },
        result);
}

std::string xml_escape(std::string string) {
    // Implementation idea stolen from https://stackoverflow.com/a/5665377
    std::string escaped;
    escaped.reserve(
        static_cast<size_t>(static_cast<double>(string.size()) * 1.1));
    for (const char& character : string) {
        switch (character) {
            case '&':
                escaped.append("&amp;");
                break;
            case '\"':
                escaped.append("&quot;");
                break;
            case '\'':
                escaped.append("&apos;");
                break;
            case '<':
                escaped.append("&lt;");
                break;
            case '>':
                escaped.append("&gt;");
                break;
            default:
                escaped.push_back(character);
                break;
        }
    }

    return escaped;
}

std::string url_encode_path(std::string path) {
    // We only need to escape a couple of special characters here. This is used
    // in the notifications as well as in the XDND proxy. We encode the reserved
    // characters mentioned here, with the exception of the forward slash:
    // https://en.wikipedia.org/wiki/Percent-encoding#Reserved_characters
    std::string escaped;
    escaped.reserve(
        static_cast<size_t>(static_cast<double>(path.size()) * 1.1));
    for (const char& character : path) {
        switch (character) {
            // Spaces are somehow in the above list, but Bitwig Studio requires
            // spaces to be escaped in the `text/uri-list` format
            case ' ':
                escaped.append("%20");
                break;
            case '!':
                escaped.append("%21");
                break;
            case '#':
                escaped.append("%23");
                break;
            case '$':
                escaped.append("%24");
                break;
            case '%':
                escaped.append("%25");
                break;
            case '&':
                escaped.append("%26");
                break;
            case '\'':
                escaped.append("%27");
                break;
            case '(':
                escaped.append("%28");
                break;
            case ')':
                escaped.append("%29");
                break;
            case '*':
                escaped.append("%2A");
                break;
            case '+':
                escaped.append("%2B");
                break;
            case ',':
                escaped.append("%2C");
                break;
            case ':':
                escaped.append("%3A");
                break;
            case ';':
                escaped.append("%3B");
                break;
            case '=':
                escaped.append("%3D");
                break;
            case '?':
                escaped.append("%3F");
                break;
            case '@':
                escaped.append("%40");
                break;
            case '[':
                escaped.append("%5B");
                break;
            case ']':
                escaped.append("%5D");
                break;
            default:
                escaped.push_back(character);
                break;
        }
    }

    return escaped;
}
