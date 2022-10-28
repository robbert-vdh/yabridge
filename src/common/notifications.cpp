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

#include <atomic>
#include <cassert>
#include <iostream>
#include <mutex>

#include <dbus/dbus.h>
#include <dlfcn.h>

#include "logging/common.h"
#include "process.h"
#include "utils.h"

constexpr char libdbus_library_name[] = "libdbus-1.so";

std::atomic<void*> libdbus_handle = nullptr;
std::mutex libdbus_mutex;

// We'll fetch all of these functions at runtime when send a first notification
decltype(dbus_connection_unref)* libdbus_connection_unref = nullptr;
decltype(dbus_bus_get)* libdbus_bus_get = nullptr;
decltype(dbus_error_free)* libdbus_error_free = nullptr;
decltype(dbus_error_init)* libdbus_error_init = nullptr;
decltype(dbus_error_is_set)* libdbus_error_is_set = nullptr;
decltype(dbus_connection_set_exit_on_disconnect)*
    libdbus_connection_set_exit_on_disconnect = nullptr;

std::unique_ptr<DBusConnection, void (*)(DBusConnection*)> libdbus_connection(
    nullptr,
    libdbus_connection_unref);

/**
 * Try to set up DBus. Returns `false` if a function could not be resolved or if
 * we could not connect to the DBus session.
 */
bool setup_libdbus() {
    // If this function is called from two threads at the same time, then we can
    // skip this. `libdbus_handle` is only set at the very end of this function
    // once every function pointer has been resolved
    std::lock_guard lock(libdbus_mutex);
    if (libdbus_handle) {
        return true;
    }

    Logger logger = Logger::create_exception_logger();

    void* handle = dlopen(libdbus_library_name, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        logger.log("Could not load '" + std::string(libdbus_library_name) +
                   "', not sending desktop notifications");
        return false;
    }

#define LOAD_FUNCTION(name)                                                 \
    do {                                                                    \
        lib##name =                                                         \
            reinterpret_cast<decltype(lib##name)>(dlsym(handle, #name));    \
        if (!(name)) {                                                      \
            logger.log("Could not find '" + std::string(#name) + "' in '" + \
                       std::string(libdbus_library_name) +                  \
                       "', not sending desktop notifications");             \
            return false;                                                   \
        }                                                                   \
    } while (false)

    LOAD_FUNCTION(dbus_connection_unref);
    LOAD_FUNCTION(dbus_bus_get);
    LOAD_FUNCTION(dbus_error_free);
    LOAD_FUNCTION(dbus_error_init);
    LOAD_FUNCTION(dbus_error_is_set);
    LOAD_FUNCTION(dbus_connection_set_exit_on_disconnect);

#undef LOAD_FUNCTION

    // With every function ready, we can try connecting to the DBus interface
    DBusError error;
    libdbus_error_init(&error);

    libdbus_connection.reset(
        libdbus_bus_get(DBusBusType::DBUS_BUS_SESSION, &error));
    if (libdbus_error_is_set(&error)) {
        assert(error.message);
        logger.log("Could not connect to DBus session bus: " +
                   std::string(error.message));
        libdbus_error_free(&error);

        return false;
    }
    assert(libdbus_connection);

    // While the connection should not be closed while this plugin is alive,
    // this does sound extremely dangerous and why is it enabled by default?
    libdbus_connection_set_exit_on_disconnect(libdbus_connection.get(), false);

    // This is only set at the very end since this indicates that everything
    // has been initialized properly
    libdbus_handle.store(handle);

    return true;
}

bool send_notification(const std::string& title,
                       const std::string body,
                       std::optional<ghc::filesystem::path> origin) {
    // The first time this function is called we'll need to set up the DBus
    // interface. Previously yabridge relied on notify-send, but some distros
    // don't install that by default.
    if (!libdbus_handle && !setup_libdbus()) {
        return false;
    }

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
