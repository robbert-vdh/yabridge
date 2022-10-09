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

#include "clap.h"

#include <codecvt>
#include <locale>

#include <clap/plugin-factory.h>

// Generated inside of the build directory
#include <version.h>

namespace fs = ghc::filesystem;

ClapPluginExtensions::ClapPluginExtensions(const clap_plugin& plugin) noexcept
    : audio_ports(static_cast<const clap_plugin_audio_ports_t*>(
          plugin.get_extension(&plugin, CLAP_EXT_AUDIO_PORTS))),
      gui(static_cast<const clap_plugin_gui_t*>(
          plugin.get_extension(&plugin, CLAP_EXT_GUI))),
      latency(static_cast<const clap_plugin_latency_t*>(
          plugin.get_extension(&plugin, CLAP_EXT_LATENCY))),
      note_ports(static_cast<const clap_plugin_note_ports_t*>(
          plugin.get_extension(&plugin, CLAP_EXT_NOTE_PORTS))),
      params(static_cast<const clap_plugin_params_t*>(
          plugin.get_extension(&plugin, CLAP_EXT_PARAMS))),
      state(static_cast<const clap_plugin_state_t*>(
          plugin.get_extension(&plugin, CLAP_EXT_STATE))),
      tail(static_cast<const clap_plugin_tail_t*>(
          plugin.get_extension(&plugin, CLAP_EXT_TAIL))) {}

ClapPluginExtensions::ClapPluginExtensions() noexcept {}

clap::plugin::SupportedPluginExtensions ClapPluginExtensions::supported()
    const noexcept {
    return clap::plugin::SupportedPluginExtensions{
        .supports_audio_ports = audio_ports != nullptr,
        .supports_gui = gui != nullptr,
        .supports_latency = latency != nullptr,
        .supports_note_ports = note_ports != nullptr,
        .supports_params = params != nullptr,
        .supports_state = state != nullptr,
        .supports_tail = tail != nullptr};
}

ClapPluginInstance::ClapPluginInstance(
    const clap_plugin* plugin,
    std::unique_ptr<clap_host_proxy> host_proxy) noexcept
    : host_proxy(std::move(host_proxy)),
      plugin((assert(plugin), plugin), plugin->destroy),
      // We may only query the supported extensions after initializing the
      // plugin
      extensions() {}

ClapBridge::ClapBridge(MainContext& main_context,
                       // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
                       std::string plugin_dll_path,
                       std::string endpoint_base_dir,
                       pid_t parent_pid)
    : HostBridge(main_context, plugin_dll_path, parent_pid),
      logger_(generic_logger_),
      plugin_handle_(LoadLibrary(plugin_dll_path.c_str()), FreeLibrary),
      entry_(plugin_handle_
                 ? reinterpret_cast<clap_plugin_entry_t*>(
                       GetProcAddress(plugin_handle_.get(), "clap_entry"))
                 : nullptr,
             [](clap_plugin_entry_t* entry) { entry->deinit(); }),
      sockets_(main_context.context_, endpoint_base_dir, false) {
    if (!plugin_handle_) {
        throw std::runtime_error(
            "Could not load the Windows .clap (.dll) file at '" +
            plugin_dll_path + "'");
    }
    if (!entry_) {
        throw std::runtime_error(
            "" + plugin_dll_path +
            "' does not export the 'clap_entry' entry point.");
    }

    if (!clap_version_is_compatible(entry_->clap_version)) {
        throw std::runtime_error(
            "" + plugin_dll_path + "' has an incompatible CLAP version (" +
            std::to_string(entry_->clap_version.major) + "." +
            std::to_string(entry_->clap_version.minor) + "." +
            std::to_string(entry_->clap_version.revision) + ").");
    }

    // CLAP plugins receive the library path in their init function. The problem
    // is that `plugin_dll_path` is a Linux path. This should be fine as all
    // Wine syscalls can work with both Windows and Linux style paths, but if
    // the plugin wants to manipulate the path then this may result in
    // unexpected behavior. Wine can convert these paths for us, but we'd get a
    // `WCHAR*` back which we must first convert back to UTF-8.
    bool init_success;
    WCHAR* dos_plugin_dll_path(wine_get_dos_file_name(plugin_dll_path.c_str()));
    if (dos_plugin_dll_path) {
        static_assert(sizeof(WCHAR) == sizeof(char16_t));
        std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>
            converter;
        const std::string converted_plugin_path =
            std::string(converter.to_bytes(std::u16string(
                reinterpret_cast<char16_t*>(dos_plugin_dll_path))));

        // This function is not optional, but if the plugin somehow does not
        // provide it and we'll call it anyways then the error will be less than
        // obvious
        assert(entry_->init);
        init_success = entry_->init(converted_plugin_path.c_str());

        // Can't use regular `free()` or `unique_ptr` here
        HeapFree(GetProcessHeap(), 0, dos_plugin_dll_path);
    } else {
        // This should never be hit, but just in case
        init_success = entry_->init(plugin_dll_path.c_str());
    }

    if (!init_success) {
        // `clap_entry->deinit()` is normally called when `entry_` is dropped,
        // but taht shouldn't happen if the entry point was never initialized.
        [[maybe_unused]] auto _ = entry_.release();
        throw std::runtime_error("'clap_entry->init()' returned false.");
    }

    sockets_.connect();

    // Fetch this instance's configuration from the plugin to finish the setup
    // process
    config_ = sockets_.plugin_host_main_thread_callback_.send_message(
        WantsConfiguration{.host_version = yabridge_git_version}, std::nullopt);

    // Allow this plugin to configure the main context's tick rate
    main_context.update_timer_interval(config_.event_loop_interval());
}

bool ClapBridge::inhibits_event_loop() noexcept {
    std::shared_lock lock(object_instances_mutex_);

    for (const auto& [instance_id, instance] : object_instances_) {
        if (!instance.is_initialized) {
            return true;
        }
    }

    return false;
}

void ClapBridge::run() {
    set_realtime_priority(true);

    sockets_.host_plugin_main_thread_control_.receive_messages(
        std::nullopt,
        overload{
            [&](const WantsConfiguration&) -> WantsConfiguration::Response {
                // FIXME: This overload shouldn't be here, but
                //        bitsery simply won't allow us to serialize the
                //        variant without it.
                return {};
            },
            [&](const clap::plugin_factory::List&)
                -> clap::plugin_factory::List::Response {
                return main_context_
                    .run_in_context([&]() {
                        plugin_factory_ =
                            static_cast<const clap_plugin_factory_t*>(
                                entry_->get_factory(CLAP_PLUGIN_FACTORY_ID));
                        if (!plugin_factory_) {
                            return clap::plugin_factory::ListResponse{
                                .descriptors = std::nullopt};
                        }

                        std::vector<clap::plugin::Descriptor> descriptors;
                        const uint32_t num_plugins =
                            plugin_factory_->get_plugin_count(plugin_factory_);
                        for (uint32_t i = 0; i < num_plugins; i++) {
                            const clap_plugin_descriptor_t* descriptor =
                                plugin_factory_->get_plugin_descriptor(
                                    plugin_factory_, i);
                            if (!descriptor) {
                                std::cerr << "Plugin returned a null pointer "
                                             "for plugin index "
                                          << i << "(" << num_plugins
                                          << " total), skipping..."
                                          << std::endl;
                                continue;
                            }

                            descriptors.push_back(*descriptor);
                        }

                        return clap::plugin_factory::ListResponse{
                            .descriptors = descriptors};
                    })
                    .get();
            },
            [&](clap::plugin_factory::Create& request)
                -> clap::plugin_factory::Create::Response {
                return main_context_
                    .run_in_context([&]() {
                        // This assertion should never be hit, but you can never
                        // be too sure!
                        assert(plugin_factory_);

                        // We need the instance ID before the instance exists.
                        // If creating the plugin fails then that's no problem
                        // since we're using sparse hash maps anyways.
                        const size_t instance_id = generate_instance_id();
                        auto host_proxy = std::make_unique<clap_host_proxy>(
                            *this, instance_id, std::move(request.host));

                        const clap_plugin_t* plugin =
                            plugin_factory_->create_plugin(
                                plugin_factory_, host_proxy->host_vtable(),
                                request.plugin_id.c_str());
                        if (plugin) {
                            register_plugin_instance(plugin,
                                                     std::move(host_proxy));
                            return clap::plugin_factory::CreateResponse{
                                .instance_id = instance_id};
                        } else {
                            return clap::plugin_factory::CreateResponse{
                                .instance_id = std::nullopt};
                        }
                    })
                    .get();
            },
            [&](clap::plugin::Init& request) -> clap::plugin::Init::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return main_context_
                    .run_in_context([&, plugin = instance.plugin.get(),
                                     &instance = instance]() {
                        // The plugin is allowed to query the same set of
                        // extensions from our host proxy that the native host
                        // supports
                        instance.host_proxy->supported_extensions_ =
                            request.supported_host_extensions;

                        const bool result = plugin->init(plugin);
                        if (result) {
                            // This mimics the same behavior we had to implement
                            // for VST2 and VST3. The Win32 message loop is
                            // completely blocked while a plugin instance has
                            // been created but not yet initialized.
                            instance.is_initialized = true;

                            // At this point we should also get the extension
                            // pointers for the plugin's supported extensions.
                            // In addition we'll send whether or not the plugin
                            // supports these extensions as booleans to the
                            // native plugin side so we can expose these same
                            // extensions to the host.
                            instance.extensions = ClapPluginExtensions(*plugin);

                            return clap::plugin::InitResponse{
                                .result = result,
                                // Similarly, we'll make the plugin's supported
                                // extensions available to the host
                                .supported_plugin_extensions =
                                    instance.extensions.supported()};
                        } else {
                            return clap::plugin::InitResponse{
                                .result = result,
                                .supported_plugin_extensions = {}};
                        }
                    })
                    .get();
            },
            [&](clap::plugin::Destroy& request)
                -> clap::plugin::Destroy::Response {
                return main_context_
                    .run_in_context([&]() {
                        // This calls `clap_plugin::destroy()` as part of
                        // cleaning up the `unique_ptr` holding the plugin
                        // instance pointer
                        unregister_plugin_instance(request.instance_id);

                        return Ack{};
                    })
                    .get();
            },
            [&](clap::plugin::Activate& request)
                -> clap::plugin::Activate::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return main_context_
                    .run_in_context([&, plugin = instance.plugin.get()]() {
                        const bool result = plugin->activate(
                            plugin, request.sample_rate,
                            request.min_frames_count, request.max_frames_count);

                        const std::optional<AudioShmBuffer::Config>
                            updated_audio_buffers_config =
                                setup_shared_audio_buffers(request.instance_id,
                                                           request);

                        return clap::plugin::ActivateResponse{
                            .result = result,
                            .updated_audio_buffers_config =
                                std::move(updated_audio_buffers_config)};
                    })
                    .get();
            },
            [&](clap::plugin::Deactivate& request)
                -> clap::plugin::Deactivate::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return main_context_
                    .run_in_context([&, plugin = instance.plugin.get()]() {
                        plugin->deactivate(plugin);

                        return Ack{};
                    })
                    .get();
            },
            [&](const clap::ext::audio_ports::plugin::Count& request)
                -> clap::ext::audio_ports::plugin::Count::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                // We'll ignore the main thread requirement for simple array
                // lookups to avoid the synchronisation costs in hot code paths
                return instance.extensions.audio_ports->count(
                    instance.plugin.get(), request.is_input);
            },
            [&](const clap::ext::audio_ports::plugin::Get& request)
                -> clap::ext::audio_ports::plugin::Get::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                // We'll ignore the main thread requirement for simple array
                // lookups to avoid the synchronisation costs in hot code paths
                clap_audio_port_info_t info{};
                if (instance.extensions.audio_ports->get(
                        instance.plugin.get(), request.index, request.is_input,
                        &info)) {
                    return clap::ext::audio_ports::plugin::GetResponse{
                        .result = info};
                } else {
                    return clap::ext::audio_ports::plugin::GetResponse{
                        .result = std::nullopt};
                }
            },
            [&](const clap::ext::gui::plugin::IsApiSupported& request)
                -> clap::ext::gui::plugin::IsApiSupported::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                // See below and the comment in `host-proxy.cpp` for why this is
                // sprinkled all over the place
                return do_mutual_recursion_on_gui_thread(
                    [&, plugin = instance.plugin.get(),
                     gui = instance.extensions.gui]() {
                        // It's a bit unnecessary to bridge the entire
                        // `is_api_supported()` function since we'll only bridge
                        // a single config (non-floating, X11), but this is
                        // makes it easier to expand in the future. The X11 API
                        // type gets translated to WIN32 for the plugin. We also
                        // prematurely return false when `is_floating` is false
                        // because we cannot set the transient window correctly
                        // when the plugin opens its own Wine window.
                        switch (request.api) {
                            case clap::ext::gui::ApiType::X11:
                            default:
                                return gui->is_api_supported(
                                    plugin, CLAP_WINDOW_API_WIN32,
                                    request.is_floating);
                                break;
                        }
                    });
            },
            [&](const clap::ext::gui::plugin::Create& request)
                -> clap::ext::gui::plugin::Create::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return do_mutual_recursion_on_gui_thread(
                    [&, plugin = instance.plugin.get(),
                     gui = instance.extensions.gui]() {
                        // We don't need to do anything here yet. The actual
                        // window is created at the final `.set_parent()` call.
                        // Like the above function, we'll translate the API type
                        // and `is_floating` will always be `false`.
                        switch (request.api) {
                            case clap::ext::gui::ApiType::X11:
                            default:
                                return gui->create(plugin,
                                                   CLAP_WINDOW_API_WIN32,
                                                   request.is_floating);
                                break;
                        }
                    });
            },
            [&](const clap::ext::gui::plugin::Destroy& request)
                -> clap::ext::gui::plugin::Destroy::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return do_mutual_recursion_on_gui_thread(
                    [&, plugin = instance.plugin.get(),
                     gui = instance.extensions.gui,
                     &editor = instance.editor]() {
                        gui->destroy(plugin);

                        // Cleanup is handled through RAII
                        editor.reset();

                        return Ack{};
                    });
            },
            [&](clap::ext::gui::plugin::SetScale& request)
                -> clap::ext::gui::plugin::SetScale::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                if (config_.editor_disable_host_scaling) {
                    std::cerr << "The host requested the editor GUI to be "
                                 "scaled by a factor of "
                              << request.scale
                              << ", but the 'editor_disable_host_scaling' "
                                 "option is enabled. Ignoring the request."
                              << std::endl;
                    return false;
                } else {
                    return do_mutual_recursion_on_gui_thread(
                        [&, plugin = instance.plugin.get(),
                         gui = instance.extensions.gui]() {
                            return gui->set_scale(plugin, request.scale);
                        });
                }
            },
            [&](const clap::ext::gui::plugin::GetSize& request)
                -> clap::ext::gui::plugin::GetSize::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return do_mutual_recursion_on_gui_thread(
                    [&, plugin = instance.plugin.get(),
                     gui = instance.extensions.gui]() {
                        uint32_t width{};
                        uint32_t height{};
                        const bool result =
                            gui->get_size(plugin, &width, &height);

                        return clap::ext::gui::plugin::GetSizeResponse{
                            .result = result, .width = width, .height = height};
                    });
            },
            [&](clap::ext::gui::plugin::CanResize& request)
                -> clap::ext::gui::plugin::CanResize::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return do_mutual_recursion_on_gui_thread(
                    [&, plugin = instance.plugin.get(),
                     gui = instance.extensions.gui]() {
                        return gui->can_resize(plugin);
                    });
            },
            [&](const clap::ext::gui::plugin::GetResizeHints& request)
                -> clap::ext::gui::plugin::GetResizeHints::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return do_mutual_recursion_on_gui_thread([&,
                                                          plugin =
                                                              instance.plugin
                                                                  .get(),
                                                          gui = instance
                                                                    .extensions
                                                                    .gui]() {
                    clap_gui_resize_hints_t hints{};
                    if (gui->get_resize_hints(plugin, &hints)) {
                        return clap::ext::gui::plugin::GetResizeHintsResponse{
                            .result = std::move(hints)};
                    } else {
                        return clap::ext::gui::plugin::GetResizeHintsResponse{
                            .result = std::nullopt};
                    }
                });
            },
            [&](const clap::ext::gui::plugin::AdjustSize& request)
                -> clap::ext::gui::plugin::AdjustSize::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return do_mutual_recursion_on_gui_thread(
                    [&, plugin = instance.plugin.get(),
                     gui = instance.extensions.gui]() {
                        uint32_t width = request.width;
                        uint32_t height = request.height;
                        const bool result =
                            gui->adjust_size(plugin, &width, &height);

                        return clap::ext::gui::plugin::AdjustSizeResponse{
                            .result = result,
                            .updated_width = width,
                            .updated_height = height};
                    });
            },
            [&](const clap::ext::gui::plugin::SetSize& request)
                -> clap::ext::gui::plugin::SetSize::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return do_mutual_recursion_on_gui_thread(
                    [&, plugin = instance.plugin.get(),
                     gui = instance.extensions.gui,
                     &editor = instance.editor]() {
                        assert(editor);

                        // HACK: We need to resize the editor window before
                        //       setting the size on the plugin. Surge XT and
                        //       presumably other CLAP JUCE Extensions plugins
                        //       will request a resize to the same size that was
                        //       just set. This causes a resize loop, so we'll
                        //       try to prevent resizes to the same size.
                        const Size old_size = editor->size();
                        editor->resize(request.width, request.height);

                        if (gui->set_size(plugin, request.width,
                                          request.height)) {
                            return true;
                        } else {
                            editor->resize(old_size.width, old_size.height);

                            return false;
                        }
                    });
            },
            [&](const clap::ext::gui::plugin::SetParent& request)
                -> clap::ext::gui::plugin::SetParent::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                // NOTE: This one in particular needs the mutual recursion
                //       because Surge XT calls this function immediately when
                //       inserting, and when the host opens the GUI at the same
                //       time this would otherwise deadlock
                return do_mutual_recursion_on_gui_thread(
                    [&, plugin = instance.plugin.get(),
                     gui = instance.extensions.gui,
                     &editor = instance.editor]() {
                        Editor& editor_instance =
                            editor.emplace(main_context_, config_,
                                           generic_logger_, request.x11_window);

                        const clap_window_t window{
                            .api = CLAP_WINDOW_API_WIN32,
                            .win32 = editor_instance.win32_handle()};
                        const bool result = gui->set_parent(plugin, &window);

                        // Set the window's initial size according to what the
                        // plugin reports. Otherwise get rid of the editor again
                        // if the plugin didn't embed itself in it.
                        if (result) {
                            uint32_t width{};
                            uint32_t height{};
                            if (gui->get_size(plugin, &width, &height)) {
                                editor->resize(width, height);
                            }

                            // NOTE: There's zero reason why the window couldn't
                            //       already be visible from the start, but
                            //       Waves V13 VST3 plugins think it would be a
                            //       splendid idea to randomly dereference null
                            //       pointers when the window is already
                            //       visible. Thanks Waves. We'll do the same
                            //       thing for CLAP plugins just to be safe
                            editor->show();
                        } else {
                            editor.reset();
                        }

                        return result;
                    });
            },
            [&](const clap::ext::gui::plugin::Show& request)
                -> clap::ext::gui::plugin::Show::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                // We don't need any special handling for our editor window, but
                // the plugin may use these functions to suspend drawing or stop
                // other tasks while the window is hdden
                return do_mutual_recursion_on_gui_thread(
                    [&, plugin = instance.plugin.get(),
                     gui = instance.extensions.gui]() {
                        return gui->show(plugin);
                    });
            },
            [&](const clap::ext::gui::plugin::Hide& request)
                -> clap::ext::gui::plugin::Hide::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return do_mutual_recursion_on_gui_thread(
                    [&, plugin = instance.plugin.get(),
                     gui = instance.extensions.gui]() {
                        return gui->hide(plugin);
                    });
            },
            [&](clap::ext::latency::plugin::Get& request)
                -> clap::ext::latency::plugin::Get::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                // We'll ignore the main thread requirement for simple lookups
                // to avoid the synchronisation costs in hot code paths
                return instance.extensions.latency->get(instance.plugin.get());
            },
            [&](const clap::ext::note_ports::plugin::Count& request)
                -> clap::ext::note_ports::plugin::Count::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                // We'll ignore the main thread requirement for simple array
                // lookups to avoid the synchronisation costs in hot code paths
                return instance.extensions.note_ports->count(
                    instance.plugin.get(), request.is_input);
            },
            [&](const clap::ext::note_ports::plugin::Get& request)
                -> clap::ext::note_ports::plugin::Get::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                // We'll ignore the main thread requirement for simple array
                // lookups to avoid the synchronisation costs in hot code paths
                clap_note_port_info_t info{};
                if (instance.extensions.note_ports->get(
                        instance.plugin.get(), request.index, request.is_input,
                        &info)) {
                    return clap::ext::note_ports::plugin::GetResponse{.result =
                                                                          info};
                } else {
                    return clap::ext::note_ports::plugin::GetResponse{
                        .result = std::nullopt};
                }
            },
            [&](const clap::ext::params::plugin::Count& request)
                -> clap::ext::params::plugin::Count::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                // We'll ignore the main thread requirement for simple array
                // lookups to avoid the synchronisation costs in hot code paths
                return instance.extensions.params->count(instance.plugin.get());
            },
            [&](const clap::ext::params::plugin::GetInfo& request)
                -> clap::ext::params::plugin::GetInfo::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                // We'll ignore the main thread requirement for simple array
                // lookups to avoid the synchronisation costs in hot code paths
                clap_param_info_t param_info{};
                if (instance.extensions.params->get_info(instance.plugin.get(),
                                                         request.param_index,
                                                         &param_info)) {
                    return clap::ext::params::plugin::GetInfoResponse{
                        .result = param_info};
                } else {
                    return clap::ext::params::plugin::GetInfoResponse{
                        .result = std::nullopt};
                }
            },
            [&](const clap::ext::params::plugin::GetValue& request)
                -> clap::ext::params::plugin::GetValue::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                // We'll ignore the main thread requirement for simple array
                // lookups to avoid the synchronisation costs in hot code paths
                double value;
                if (instance.extensions.params->get_value(
                        instance.plugin.get(), request.param_id, &value)) {
                    return clap::ext::params::plugin::GetValueResponse{
                        .result = value};
                } else {
                    return clap::ext::params::plugin::GetValueResponse{
                        .result = std::nullopt};
                }
            },
            [&](const clap::ext::params::plugin::ValueToText& request)
                -> clap::ext::params::plugin::ValueToText::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                // We'll ignore the main thread requirement for simple array
                // lookups to avoid the synchronisation costs in hot code paths
                std::array<char, 1024> display{0};
                if (instance.extensions.params->value_to_text(
                        instance.plugin.get(), request.param_id, request.value,
                        display.data(), display.size())) {
                    return clap::ext::params::plugin::ValueToTextResponse{
                        .result = display.data()};
                } else {
                    return clap::ext::params::plugin::ValueToTextResponse{
                        .result = std::nullopt};
                }
            },
            [&](const clap::ext::params::plugin::TextToValue& request)
                -> clap::ext::params::plugin::TextToValue::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                // We'll ignore the main thread requirement for simple array
                // lookups to avoid the synchronisation costs in hot code paths
                double value;
                if (instance.extensions.params->text_to_value(
                        instance.plugin.get(), request.param_id,
                        request.display.c_str(), &value)) {
                    return clap::ext::params::plugin::TextToValueResponse{
                        .result = value};
                } else {
                    return clap::ext::params::plugin::TextToValueResponse{
                        .result = std::nullopt};
                }
            },
            [&](clap::ext::state::plugin::Save& request)
                -> clap::ext::state::plugin::Save::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return main_context_
                    .run_in_context([&, plugin = instance.plugin.get(),
                                     state = instance.extensions.state]() {
                        clap::stream::Stream stream{};
                        if (state->save(plugin, stream.ostream())) {
                            return clap::ext::state::plugin::SaveResponse{
                                .result = std::move(stream)};
                        } else {
                            return clap::ext::state::plugin::SaveResponse{
                                .result = std::nullopt};
                        }
                    })
                    .get();
            },
            [&](clap::ext::state::plugin::Load& request)
                -> clap::ext::state::plugin::Load::Response {
                const auto& [instance, _] = get_instance(request.instance_id);

                return main_context_
                    .run_in_context([&, plugin = instance.plugin.get(),
                                     state = instance.extensions.state]() {
                        return state->load(plugin, request.stream.istream());
                    })
                    .get();
            },
        });
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool ClapBridge::resize_editor(size_t instance_id,
                               uint16_t width,
                               uint16_t height) {
    const auto& [instance, _] = get_instance(instance_id);

    if (instance.editor) {
        instance.editor->resize(width, height);
        return true;
    } else {
        return false;
    }
}

std::optional<Size> ClapBridge::editor_size(size_t instance_id) {
    const auto& [instance, _] = get_instance(instance_id);

    if (instance.editor) {
        return instance.editor->size();
    } else {
        return std::nullopt;
    }
}

void ClapBridge::close_sockets() {
    sockets_.close();
}

std::pair<ClapPluginInstance&, std::shared_lock<std::shared_mutex>>
ClapBridge::get_instance(size_t instance_id) noexcept {
    std::shared_lock lock(object_instances_mutex_);

    return std::pair<ClapPluginInstance&, std::shared_lock<std::shared_mutex>>(
        object_instances_.at(instance_id), std::move(lock));
}

size_t ClapBridge::generate_instance_id() noexcept {
    return current_instance_id_.fetch_add(1);
}

std::optional<AudioShmBuffer::Config> ClapBridge::setup_shared_audio_buffers(
    size_t instance_id,
    const clap::plugin::Activate& activate_request) {
    const auto& [instance, _] = get_instance(instance_id);

    const clap_plugin_t* plugin = instance.plugin.get();
    const clap_plugin_audio_ports_t* audio_ports =
        instance.extensions.audio_ports;
    if (!audio_ports) {
        return std::nullopt;
    }

    // We'll query the plugin for its audio port layouts, and then create
    // calculate the offsets in a large memory buffer for the different audio
    // channels. The offsets for each audio channel are in bytes because CLAP
    // allows the host to send mixed 32-bit and 64-bit audio if the plugin
    // advertises supporting 64-bit audio. Because of that we'll allocate enough
    // space for double precision audio when the port supports it, and then
    // we'll simply only use the first half of that space if the host sends
    // 32-bit audio.
    uint32_t current_offset = 0;
    auto create_bus_offsets = [&](bool is_input) {
        const uint32_t num_ports = audio_ports->count(plugin, is_input);

        std::vector<std::vector<uint32_t>> offsets(num_ports);
        for (uint32_t port = 0; port < num_ports; port++) {
            clap_audio_port_info_t info{};
            assert(audio_ports->get(plugin, port, is_input, &info));

            // If the audio port supports 64-bit audio, then we should allocate
            // enough memory for that
            const size_t sample_size =
                (info.flags & CLAP_AUDIO_PORT_SUPPORTS_64BITS) != 0
                    ? sizeof(double)
                    : sizeof(float);

            offsets[port].resize(info.channel_count);
            for (size_t channel = 0; channel < info.channel_count; channel++) {
                offsets[port][channel] = current_offset;
                current_offset +=
                    activate_request.max_frames_count * sample_size;
            }
        }

        return offsets;
    };

    // Creating the audio buffer offsets for every channel in every bus will
    // advance `current_offset` to keep pointing to the starting position for
    // the next channel
    const auto input_bus_offsets = create_bus_offsets(true);
    const auto output_bus_offsets = create_bus_offsets(false);
    const uint32_t buffer_size = current_offset;

    // If this function has been called previously and the size did not change,
    // then we should not do any work
    if (instance.process_buffers &&
        instance.process_buffers->config_.size == buffer_size) {
        return std::nullopt;
    }

    // We'll set up these shared memory buffers on the Wine side first, and then
    // when this request returns we'll do the same thing on the native plugin
    // side
    AudioShmBuffer::Config buffer_config{
        .name = sockets_.base_dir_.filename().string() + "-" +
                std::to_string(instance_id),
        .size = buffer_size,
        .input_offsets = std::move(input_bus_offsets),
        .output_offsets = std::move(output_bus_offsets)};
    if (!instance.process_buffers) {
        instance.process_buffers.emplace(buffer_config);
    } else {
        instance.process_buffers->resize(buffer_config);
    }

    // After setting up the shared memory buffer, we need to create a vector of
    // channel audio pointers for every bus. These will then be assigned to the
    // `AudioBusBuffers` objects in the `ClapProcess` struct in
    // `ClapProcess::reconstruct()` before passing the reconstructed process
    // data to `clap_plugin::process()`.
    auto set_port_pointers =
        [&]<std::invocable<uint32_t, uint32_t> F>(
            std::vector<std::vector<void*>>& port_pointers,
            const std::vector<std::vector<uint32_t>>& offsets,
            F&& get_channel_pointer) {
            port_pointers.resize(offsets.size());
            for (size_t port = 0; port < offsets.size(); port++) {
                port_pointers[port].resize(offsets[port].size());
                for (size_t channel = 0; channel < offsets[port].size();
                     channel++) {
                    port_pointers[port][channel] =
                        get_channel_pointer(port, channel);
                }
            }
        };

    set_port_pointers(instance.process_buffers_input_pointers,
                      instance.process_buffers->config_.input_offsets,
                      [&process_buffers = instance.process_buffers](
                          uint32_t port, uint32_t channel) {
                          // This can be treated as either a `double*` or a
                          // `float*` depending on what the port supports and
                          // what the host gives us
                          return process_buffers->input_channel_ptr<void>(
                              port, channel);
                      });
    set_port_pointers(instance.process_buffers_output_pointers,
                      instance.process_buffers->config_.output_offsets,
                      [&process_buffers = instance.process_buffers](
                          uint32_t port, uint32_t channel) {
                          return process_buffers->output_channel_ptr<void>(
                              port, channel);
                      });

    return buffer_config;
}

void ClapBridge::register_plugin_instance(
    const clap_plugin* plugin,
    std::unique_ptr<clap_host_proxy> host_proxy) {
    std::unique_lock lock(object_instances_mutex_);

    assert(plugin);
    assert(host_proxy);

    // This instance ID has already been generated because the host proxy has to
    // be created before the plugin instance
    const size_t instance_id = host_proxy->owner_instance_id();
    object_instances_.emplace(
        instance_id, ClapPluginInstance(plugin, std::move(host_proxy)));

    // Every plugin instance gets its own audio thread along with sockets for
    // host->plugin control messages and plugin->host callbacks
    std::promise<void> socket_listening_latch;
    object_instances_.at(instance_id)
        .audio_thread_handler = Win32Thread([&, instance_id]() {
        set_realtime_priority(true);

        // XXX: Like with VST2 worker threads, when using plugin groups the
        //      thread names from different plugins will clash. Not a huge
        //      deal probably, since duplicate thread names are still more
        //      useful than no thread names.
        const std::string thread_name = "audio-" + std::to_string(instance_id);
        pthread_setname_np(pthread_self(), thread_name.c_str());

        sockets_.add_audio_thread_and_listen_control(
            instance_id, socket_listening_latch,
            overload{
                [&](const clap::plugin::StartProcessing& request)
                    -> clap::plugin::StartProcessing::Response {
                    const auto& [instance, _] =
                        get_instance(request.instance_id);

                    return instance.plugin->start_processing(
                        instance.plugin.get());
                },
                [&](const clap::plugin::StopProcessing& request)
                    -> clap::plugin::StopProcessing::Response {
                    const auto& [instance, _] =
                        get_instance(request.instance_id);

                    instance.plugin->stop_processing(instance.plugin.get());

                    return Ack{};
                },
                [&](const clap::plugin::Reset& request)
                    -> clap::plugin::Reset::Response {
                    const auto& [instance, _] =
                        get_instance(request.instance_id);

                    instance.plugin->reset(instance.plugin.get());

                    return Ack{};
                },
                [&](const MessageReference<clap::plugin::Process>& request_ref)
                    -> clap::plugin::Process::Response {
                    // NOTE: To prevent allocations we keep this actual
                    //       `clap::plugin::Process` object around as part of a
                    //       static thread local `ClapAudioThreadControlRequest`
                    //       object, and we only store a reference to it in our
                    //       variant (this is done during the deserialization in
                    //       `bitsery::ext::MessageReference`)
                    clap::plugin::Process& request = request_ref.get();

                    // As suggested by Jack Winter, we'll synchronize this
                    // thread's audio processing priority with that of the
                    // host's audio thread every once in a while
                    if (request.new_realtime_priority) {
                        set_realtime_priority(true,
                                              *request.new_realtime_priority);
                    }

                    const auto& [instance, _] =
                        get_instance(request.instance_id);

                    // Most plugins will already enable FTZ, but there are a
                    // handful of plugins that don't that suffer from extreme
                    // DSP load increases when they start producing denormals
                    ScopedFlushToZero ftz_guard;

                    // The actual audio is stored in the shared memory
                    // buffers, so the reconstruction function will need to
                    // know where it should point the `AudioBusBuffers` to
                    // TODO: Once we add the render extension, process on the
                    //       main thread when doing offline rendering
                    auto& reconstructed = request.process.reconstruct(
                        instance.process_buffers_input_pointers,
                        instance.process_buffers_output_pointers);
                    clap_process_status result = instance.plugin->process(
                        instance.plugin.get(), &reconstructed);

                    return clap::plugin::ProcessResponse{
                        .result = result,
                        .output_data = request.process.create_response()};
                },
                [&](clap::ext::params::plugin::Flush& request)
                    -> clap::ext::params::plugin::Flush::Response {
                    const auto& [instance, _] =
                        get_instance(request.instance_id);

                    clap::events::EventList out{};
                    instance.extensions.params->flush(instance.plugin.get(),
                                                      request.in.input_events(),
                                                      out.output_events());

                    return clap::ext::params::plugin::FlushResponse{
                        .out = std::move(out)};
                },
                [&](const clap::ext::tail::plugin::Get& request)
                    -> clap::ext::tail::plugin::Get::Response {
                    const auto& [instance, _] =
                        get_instance(request.instance_id);

                    return instance.extensions.tail->get(instance.plugin.get());
                },
            });
    });

    // Wait for the new socket to be listening on before continuing. Otherwise
    // the native plugin may try to connect to it before our thread is up and
    // running.
    socket_listening_latch.get_future().wait();
}

void ClapBridge::unregister_plugin_instance(size_t instance_id) {
    sockets_.remove_audio_thread(instance_id);

    // Remove the instance from within the main IO context so
    // removing it doesn't interfere with the Win32 message loop
    // NOTE: This will implicitly run `clap_plugin::destroy()` as part of the
    //       `unique_ptr`'s cleanup
    main_context_
        .run_in_context([&, instance_id]() -> void {
            std::unique_lock lock(object_instances_mutex_);
            object_instances_.erase(instance_id);
        })
        .wait();
}
