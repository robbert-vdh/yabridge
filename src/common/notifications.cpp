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
constexpr char libdbus_library_fallback_name[] = "libdbus-1.so.3";

std::atomic<void*> libdbus_handle = nullptr;
std::mutex libdbus_mutex;

// We'll fetch all of these functions at runtime when send a first notification
#define LIBDBUS_FUNCTIONS                     \
    X(dbus_bus_get)                           \
    X(dbus_connection_flush)                  \
    X(dbus_connection_send)                   \
    X(dbus_connection_set_exit_on_disconnect) \
    X(dbus_connection_unref)                  \
    X(dbus_error_free)                        \
    X(dbus_error_init)                        \
    X(dbus_error_is_set)                      \
    X(dbus_message_get_serial)                \
    X(dbus_message_iter_append_basic)         \
    X(dbus_message_iter_close_container)      \
    X(dbus_message_iter_init_append)          \
    X(dbus_message_iter_open_container)       \
    X(dbus_message_new_method_call)           \
    X(dbus_message_unref)

#define X(name) decltype(name)* lib##name = nullptr;
LIBDBUS_FUNCTIONS
#undef X

std::unique_ptr<DBusConnection, void (*)(DBusConnection*)> libdbus_connection(
    nullptr,
    libdbus_connection_unref);

/**
 * Try to set up D-Bus. Returns `false` if a function could not be resolved or
 * if we could not connect to the D-Bus session.
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
        handle = dlopen(libdbus_library_fallback_name, RTLD_LAZY | RTLD_LOCAL);
        if (!handle) {
            logger.log("Could not load '" + std::string(libdbus_library_name) +
                       "', not sending desktop notifications");
            return false;
        }
    }

#define X(name)                                                             \
    do {                                                                    \
        lib##name =                                                         \
            reinterpret_cast<decltype(lib##name)>(dlsym(handle, #name));    \
        if (!lib##name) {                                                   \
            logger.log("Could not find '" + std::string(#name) + "' in '" + \
                       std::string(libdbus_library_name) +                  \
                       "', not sending desktop notifications");             \
            return false;                                                   \
        }                                                                   \
    } while (false);

    LIBDBUS_FUNCTIONS

#undef X

    // With every function ready, we can try connecting to the D-Bus interface
    DBusError error;
    libdbus_error_init(&error);

    libdbus_connection.reset(
        libdbus_bus_get(DBusBusType::DBUS_BUS_SESSION, &error));
    if (libdbus_error_is_set(&error)) {
        assert(error.message);
        logger.log("Could not connect to D-Bus session bus: " +
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
    // The first time this function is called we'll need to set up the D-Bus
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

    // Actually sending the notification is done directly using the D-Bus API.
    std::unique_ptr<DBusMessage, void (*)(DBusMessage*)> message(
        libdbus_message_new_method_call(
            "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
            "org.freedesktop.Notifications", "Notify"),
        libdbus_message_unref);
    assert(message);

    // Can't use `dbus_message_append_args` because that doesn't support
    // dictionaries
    DBusMessageIter iter;
    libdbus_message_iter_init_append(message.get(), &iter);

    const char* app_name = "yabridge";
    assert(
        libdbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &app_name));

    // It would be nice to be able to replace old notifications so we don't
    // accidentally spam the user when every plugin outputs the same message,
    // but we can't really do this since during plugin scanning every plugin
    // will likely be loaded in a fresh process
    const dbus_uint32_t replaces_id = 0;
    assert(libdbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32,
                                             &replaces_id));

    const char* app_icon = "";
    assert(
        libdbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &app_icon));

    const char* title_cstr = title.c_str();
    assert(libdbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING,
                                             &title_cstr));

    const std::string formatted_body_str = formatted_body.str();
    const char* formatted_body_cstr = formatted_body_str.c_str();
    assert(libdbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING,
                                             &formatted_body_cstr));

    // Our actions array is empty
    DBusMessageIter array_iter;
    assert(libdbus_message_iter_open_container(
        &iter, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING, &array_iter));
    assert(libdbus_message_iter_close_container(&iter, &array_iter));

    // We also don't have any hints, but we can't use the simple
    // `libdbus_message_append_args` API because we can't use it to add an empty
    // hints dictionary
    assert(libdbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}",
                                               &array_iter));
    assert(libdbus_message_iter_close_container(&iter, &array_iter));

    // -1 is an implementation specific default duration
    const dbus_int32_t expiry_timeout = -1;
    assert(libdbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32,
                                             &expiry_timeout));

    // And after all of that we can finally send the actual notification
    dbus_uint32_t message_serial = libdbus_message_get_serial(message.get());
    const bool result = libdbus_connection_send(libdbus_connection.get(),
                                                message.get(), &message_serial);
    libdbus_connection_flush(libdbus_connection.get());

    return result;
}
