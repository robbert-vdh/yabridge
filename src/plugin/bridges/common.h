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

#include <future>
#include <iomanip>

#include <sys/resource.h>

// Generated inside of the build directory
#include <config.h>
#include <version.h>

#include "../../common/configuration.h"
#include "../../common/linking.h"
#include "../../common/notifications.h"
#include "../../common/utils.h"
#include "../host-process.h"

/**
 * If the amount of lockable memory is below this, then we'll warn about it
 * during startup. Otherwise we may run into issues when mapping shared memory
 * for plugins with a lot of inputs or outputs. We would of course prefer this
 * to just be set to `RLIM_INFINITY`, but this seems like a reasonable amount.
 */
constexpr int memlock_min_safe_threshold = 256 << 20;

/**
 * PipeWire uses rtkit, and both set `RLIMIT_RTTIME` to some low value. Normally
 * this is kept at unlimited, and low values can cause the host process to get
 * terminated during initialization because some plugins may take longer than
 * the default 200ms to load. We'll show a warning when the realtime CPU time
 * limit is not unlimited (`-1`/`RLIM_INFINITY`) and below this value.
 */
constexpr int rttime_min_safe_threshold = 30'000'000;

/**
 * Handles all common operations for hosting plugins such as initializing up the
 * plugin host process, setting up the logger, and logging debug information on
 * startup.
 *
 * @tparam Sockets the `Sockets` implementation to use. We have to initialize it
 *   here because we need to pass it to our `HostProcess`.
 */
template <std::derived_from<Sockets> TSockets>
class PluginBridge {
   public:
    /**
     * Sets up everything needed to start the host process. Classes deriving
     * from this should call `log_init_message()` and
     * `connect_sockets_guarded()` themselves after their initialization list.
     *
     * @param plugin_type The type of the plugin we're handling.
     * @param plugin_path The path to the **native** plugin library `.so` file.
     *   This is used to determine the path to the Windows plugin library we
     *   should load.
     * @param create_socket_instance A function to create a socket instance.
     *   Using a lambda here feels wrong, but I can't think of a better
     *   solution right now.
     *
     * @throw std::runtime_error Thrown when the Wine plugin host could not be
     *   found, or if it could not locate and load a corresponding Windows
     *   plugin library.
     */
    template <
        invocable_returning<TSockets, asio::io_context&, const PluginInfo&> F>
    PluginBridge(PluginType plugin_type,
                 const ghc::filesystem::path& plugin_path,
                 F&& create_socket_instance)
        // This is still correct for VST3 plugins because we can configure an
        // entire directory (the module's bundle) at once
        : config_(load_config_for(plugin_path)),
          info_(plugin_type, plugin_path, config_.vst3_prefer_32bit),
          io_context_(),
          sockets_(create_socket_instance(io_context_, info_)),
          generic_logger_(Logger::create_from_environment(
              create_logger_prefix(sockets_.base_dir_))),
          plugin_host_(
              config_.group
                  ? std::unique_ptr<HostProcess>(std::make_unique<GroupHost>(
                        io_context_,
                        generic_logger_,
                        config_,
                        sockets_,
                        info_,
                        HostRequest{
                            .plugin_type = plugin_type,
                            .plugin_path = info_.windows_plugin_path_.string(),
                            .endpoint_base_dir = sockets_.base_dir_.string(),
                            .parent_pid = getpid()}))
                  : std::unique_ptr<HostProcess>(
                        std::make_unique<IndividualHost>(
                            io_context_,
                            generic_logger_,
                            config_,
                            sockets_,
                            info_,
                            HostRequest{.plugin_type = plugin_type,
                                        .plugin_path =
                                            info_.windows_plugin_path_.string(),
                                        .endpoint_base_dir =
                                            sockets_.base_dir_.string(),
                                        .parent_pid = getpid()}))),
          has_realtime_priority_(has_realtime_priority_promise_.get_future()),
          wine_io_handler_([&]() {
              // We no longer run this thread with realtime scheduling because
              // plugins that produce a lot of FIXMEs could in theory cause
              // dropouts that way, but we still need to run this from a thread
              // to check whether we support it
              has_realtime_priority_promise_.set_value(
                  set_realtime_priority(true));
              set_realtime_priority(false);
              pthread_setname_np(pthread_self(), "wine-stdio");

              io_context_.run();
          }) {}

    virtual ~PluginBridge() noexcept {};

   protected:
    /**
     * Format and log all relevant debug information during initialization.
     */
    void log_init_message() {
        std::stringstream init_msg;

        init_msg << "Initializing yabridge version " << yabridge_git_version
#ifdef __i386__
                 << " (32-bit build)"
#endif
                 << std::endl;
        init_msg << "library:       '" << get_this_file_location().string()
                 << "'" << std::endl;
        init_msg << "host:          '" << plugin_host_->path().string() << "'"
                 << std::endl;
        init_msg << "plugin:        '" << info_.windows_plugin_path_.string()
                 << "'" << std::endl;
        init_msg << "plugin type:   '"
                 << plugin_type_to_string(info_.plugin_type_) << "'"
                 << std::endl;
        init_msg << "realtime:      ";
        if (has_realtime_priority_.get()) {
            // Warn if `RLIMIT_RTTIME` is set to some low value. This can happen
            // when using PipeWire.
            if (auto rttime_limit = get_rttime_limit()) {
                if (*rttime_limit != RLIM_INFINITY &&
                    *rttime_limit < rttime_min_safe_threshold) {
                    init_msg << "'yes-ish, see below'" << std::endl;
                    init_msg << std::endl;
                    init_msg << "   RLIMIT_RTTIME is set to " << *rttime_limit
                             << " us. This can happen when" << std::endl;
                    init_msg << "   using PipeWire. yabridge may crash when "
                                "loading plugins"
                             << std::endl;
                    init_msg << "   until you fix this. Check the readme for "
                                "instructions"
                             << std::endl;
                    init_msg << "   on how to do that." << std::endl;
                    init_msg << std::endl;

                    send_notification(
                        "Low RTTIME limit detected",
                        "RLIMIT_RTTIME is set to " +
                            std::to_string(*rttime_limit) +
                            " us. This can happen when using PipeWire's JACK "
                            "backend with RTKit instead of regular realtime "
                            "scheduling. Some plugins may crash during "
                            "initialization because of this, so it's "
                            "recommended to set up proper realtime privileges "
                            "for your user. Check the readme for "
                            "instructions on how to do that.",
                        std::nullopt);
                } else {
                    init_msg << "'yes'" << std::endl;
                }
            } else {
                init_msg << "'WARNING: Could not fetch RLIMIT_RTTIME'"
                         << std::endl;
            }
        } else {
            init_msg << "'no'" << std::endl;
        }
        // This doesn't really fit here, but this seems like the place to warn
        // about low memlock limits. Because this is meant to just be a helpful
        // warning, we won't print anything at all when there's no need to.
        if (auto memlock_limit = get_memlock_limit()) {
            if (*memlock_limit != RLIM_INFINITY &&
                *memlock_limit < memlock_min_safe_threshold) {
                init_msg << "memlock limit: '" << *memlock_limit
                         << " bytes, see below'" << std::endl;
                init_msg << std::endl;
                init_msg
                    << "   With a low memory locking limit, yabridge may not be"
                    << std::endl;
                init_msg << "   be able to map enough shared memory for its "
                            "audio buffers."
                         << std::endl;
                init_msg
                    << "   Plugins with many input or output channels may cause"
                    << std::endl;
                init_msg << "   yabridge to crash until you fix this. Check the"
                         << std::endl;
                init_msg << "   readme for instructions on how to do that."
                         << std::endl;
                init_msg << std::endl;

                send_notification(
                    "Low memory locking limit detected",
                    "The current memlock limit is set to " +
                        std::to_string(*memlock_limit) +
                        " bytes. This means that you have not yet set up "
                        "realtime privileges for your user, and some plugins "
                        "may cause your DAW to crash until you fix this. Check "
                        "the readme for instructions on how to do that.",
                    std::nullopt);
            }
        } else {
            init_msg
                << "memlock limit: 'WARNING: Could not fetch RLIMIT_MEMLOCK'"
                << std::endl;
        }
        init_msg << "sockets:       '" << sockets_.base_dir_.string() << "'"
                 << std::endl;

        init_msg << "wine prefix:   '";
        std::visit(
            overload{
                [&](const OverridenWinePrefix& prefix) {
                    init_msg << prefix.value.string() << " <overridden>";
                },
                [&](const ghc::filesystem::path& prefix) {
                    init_msg << prefix.string();
                },
                [&](const DefaultWinePrefix&) { init_msg << "<default>"; },
            },
            info_.wine_prefix_);
        init_msg << "'" << std::endl;

        init_msg << "wine version:  '" << info_.wine_version() << "'"
                 << std::endl;
        init_msg << std::endl;

        // Print the path to the currently loaded configuration file and all
        // settings in use. Printing the matched glob pattern could also be
        // useful but it'll be very noisy and it's likely going to be clear from
        // the shown values anyways.
        init_msg << "config from:   '";
        if (config_.matched_file && config_.matched_pattern) {
            init_msg << config_.matched_file->string() << ", section \""
                     << *config_.matched_pattern << "\"";
        } else {
            init_msg << "<defaults>";
        }
        init_msg << "'" << std::endl;

        init_msg << "hosting mode:  '";
        if (config_.group) {
            init_msg << "plugin group \"" << *config_.group << "\"";
        } else {
            init_msg << "individually";
        }
        switch (info_.plugin_arch_) {
            case LibArchitecture::dll_32:
                init_msg << ", 32-bit";
                break;
            case LibArchitecture::dll_64:
                init_msg << ", 64-bit";
                break;
        }
        init_msg << "'" << std::endl;

        init_msg << "other options: ";
        std::vector<std::string> other_options;
        if (config_.disable_pipes) {
            other_options.push_back(
                "hack: pipes disabled, plugin output will go to \"" +
                config_.disable_pipes->string() + "\"");
        }
        if (config_.editor_coordinate_hack) {
            other_options.push_back("editor: coordinate hack");
        }
        if (config_.editor_force_dnd) {
            other_options.push_back("editor: force drag-and-drop");
        }
        if (config_.editor_xembed) {
            other_options.push_back("editor: XEmbed");
        }
        if (config_.frame_rate) {
            std::ostringstream option;
            option << "frame rate: " << std::setprecision(2)
                   << *config_.frame_rate << " fps";
            other_options.push_back(option.str());
        }
        if (config_.hide_daw) {
            other_options.push_back("hack: hide DAW name");
        }
        if (config_.vst3_no_scaling) {
            other_options.push_back("vst3: no GUI scaling");
        }
        if (config_.vst3_prefer_32bit) {
            other_options.push_back("vst3: prefer 32-bit");
        }
        if (!other_options.empty()) {
            init_msg << join_quoted_strings(other_options) << std::endl;
        } else {
            init_msg << "'<none>'" << std::endl;
        }

        // To make debugging easier, we'll print both unrecognized options (that
        // might be left over when an option gets removed) as well as options
        // have the wrong argument types
        if (!config_.invalid_options.empty()) {
            init_msg << "invalid arguments: "
                     << join_quoted_strings(config_.invalid_options)
                     << " (check the readme for more information)" << std::endl;
        }
        if (!config_.unknown_options.empty()) {
            init_msg << "unrecognized options: "
                     << join_quoted_strings(config_.unknown_options)
                     << std::endl;
        }
        init_msg << std::endl;

        // Include a list of enabled compile-tiem features, mostly to make debug
        // logs more useful
        init_msg << "Enabled features:" << std::endl;
#ifdef WITH_BITBRIDGE
        init_msg << "- bitbridge support" << std::endl;
#endif
#ifdef WITH_WINEDBG
        init_msg << "- winedbg" << std::endl;
#endif
#ifdef WITH_VST3
        init_msg << "- VST3 support" << std::endl;
#endif
#if !(defined(WITH_BITBRIDGE) || defined(WITH_WINEDBG) || defined(WITH_VST3))
        init_msg << "  <none>" << std::endl;
#endif
        init_msg << std::endl;

        for (std::string line = ""; std::getline(init_msg, line);) {
            generic_logger_.log(line);
        }
    }

    /**
     * Connect the sockets, while starting another thread that will terminate
     * the plugin (through `std::terminate`/SIGABRT) when the host process fails
     * to start. This is the only way to stop listening on our sockets without
     * moving everything over to asynchronous listeners (which may actually be a
     * good idea just for this use case). Otherwise the plugin would be stuck
     * loading indefinitely when Wine is not configured correctly.
     *
     * TODO: Asynchronously connect our sockets so we can interrupt it, maybe
     */
    void connect_sockets_guarded() {
#ifndef WITH_WINEDBG
        // If the Wine process fails to start, then nothing will connect to the
        // sockets and we'll be hanging here indefinitely. To prevent this,
        // we'll periodically poll whether the Wine process is still running,
        // and throw when it is not. The alternative would be to rewrite this to
        // using `async_accept`, Asio timers, and another IO context, but
        // I feel like this a much simpler solution.
        host_watchdog_handler_ = std::jthread([&](std::stop_token st) {
            using namespace std::literals::chrono_literals;

            pthread_setname_np(pthread_self(), "watchdog");

            while (!st.stop_requested()) {
                if (!plugin_host_->running()) {
                    generic_logger_.log(
                        "The Wine host process has exited unexpectedly. Check "
                        "the output above for more information.");

                    // Also show a desktop notification so users running from
                    // the GUI get a heads up
                    // FIXME: Go through these messages and update them to
                    //        reflect the chainloading changes
                    send_notification(
                        "Failed to start the Wine plugin host",
                        "Check yabridge's output for more information on what "
                        "went wrong. You may need to rerun your DAW from a "
                        "terminal and restart the plugin scanning process to "
                        "see the error.",
                        info_.native_library_path_);

                    std::terminate();
                }

                std::this_thread::sleep_for(20ms);
            }
        });
#endif

        sockets_.connect();
#ifndef WITH_WINEDBG
        host_watchdog_handler_.request_stop();
#endif
    }

    /**
     * Show a desktop notification if the Wine plugin host is using a different
     * version of yabridge than this library. Yabridge may still work (and we do
     * this often during development), but at some point a request may fail the
     * plugin and the host are out of sync.
     */
    void warn_on_version_mismatch(const std::string& host_version) {
        if (host_version != yabridge_git_version) {
            generic_logger_.log(
                "WARNING: The host application's version does not match");
            generic_logger_.log(
                "         this plugin's. If you just updated yabridge, then");
            generic_logger_.log(
                "         you may need rerun 'yabridgectl sync' first to");
            generic_logger_.log("         update your plugins.");

            // FIXME: Go through these messages and update them to reflect the
            //        chainloading changes
            send_notification(
                "Version mismatch",
                "If you just updated yabridge, then you may need "
                "to rerun 'yabridgectl sync' first to update your plugins.",
                info_.native_library_path_);
        }
    }

    /**
     * The configuration for this instance of yabridge. Set based on the values
     * from a `yabridge.toml`, if it exists.
     *
     * @see ../utils.h:load_config_for
     */
    Configuration config_;

    /**
     * Information about the plugin we're bridging.
     */
    const PluginInfo info_;

    asio::io_context io_context_;

    /**
     * The sockets used for communication with the Wine process.
     *
     * @remark `sockets_.connect()` should not be called directly.
     * `connect_sockets_guarded()` should be used instead.
     *
     * @see PluginBridge::connect_sockets_guarded
     */
    TSockets sockets_;

    /**
     * The logging facility used for this instance of yabridge. See
     * `Logger::create_from_env()` for how this is configured.
     *
     * @see Logger::create_from_env
     */
    Logger generic_logger_;

    /**
     * The Wine process hosting our plugins. In the case of group hosts a
     * `PluginBridge` instance doesn't actually own a process, but rather either
     * spawns a new detached process or it connects to an existing one.
     */
    std::unique_ptr<HostProcess> plugin_host_;

   private:
    /**
     * The promise belonging to `has_realtime_priority_` below.
     */
    std::promise<bool> has_realtime_priority_promise_;

   public:
    /**
     * Whether this process runs with realtime priority. This is set on the
     * thread that's relaying STDOUT and STDERR output from Wine, hence the need
     * for a future. We won't change the scheduler properties on the thread
     * that's initializing the plugin because some DAWs may do that from the UI
     * thread.
     */
    std::future<bool> has_realtime_priority_;

    /**
     * Runs the Asio `io_context_` thread for logging the Wine process
     * STDOUT and STDERR messages.
     */
    std::jthread wine_io_handler_;

   private:
    /**
     * A thread used during the initialisation process to terminate listening on
     * the sockets if the Wine process cannot start for whatever reason. This
     * has to be defined here instead of in the constructor we can't simply
     * detach the thread as it has to check whether the VST host is still
     * running.
     */
    std::jthread host_watchdog_handler_;
};
