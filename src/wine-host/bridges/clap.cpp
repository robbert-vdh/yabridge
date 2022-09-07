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

// TODO: Query extensions in the initializer list
ClapPluginExtensions::ClapPluginExtensions(const clap_plugin& plugin) noexcept {
}

ClapPluginInstance::ClapPluginInstance(
    const clap_plugin* plugin,
    std::unique_ptr<clap_host_proxy> host_proxy) noexcept
    : host_proxy(std::move(host_proxy)),
      plugin((assert(plugin), plugin), plugin->destroy),
      extensions(*plugin) {}

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
                                (entry_->get_factory)(CLAP_PLUGIN_FACTORY_ID));
                        if (!plugin_factory_) {
                            return clap::plugin_factory::ListResponse{
                                .descriptors = std::nullopt};
                        }

                        std::vector<clap::plugin::Descriptor> descriptors;
                        const uint32_t num_plugins =
                            (plugin_factory_->get_plugin_count)(
                                plugin_factory_);
                        for (uint32_t i = 0; i < num_plugins; i++) {
                            const clap_plugin_descriptor_t* descriptor =
                                (plugin_factory_->get_plugin_descriptor)(
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
        });
}

// TODO: Implement this
// bool ClapBridge::maybe_resize_editor(size_t instance_id,
//                                      const Steinberg::ViewRect& new_size) {
//     const auto& [instance, _] = get_instance(instance_id);

//     if (instance.editor) {
//         instance.editor->resize(new_size.getWidth(), new_size.getHeight());
//         return true;
//     } else {
//         return false;
//     }
// }

void ClapBridge::close_sockets() {
    sockets_.close();
}

size_t ClapBridge::generate_instance_id() noexcept {
    return current_instance_id_.fetch_add(1);
}

std::pair<ClapPluginInstance&, std::shared_lock<std::shared_mutex>>
ClapBridge::get_instance(size_t instance_id) noexcept {
    std::shared_lock lock(object_instances_mutex_);

    return std::pair<ClapPluginInstance&, std::shared_lock<std::shared_mutex>>(
        object_instances_.at(instance_id), std::move(lock));
}

// TODO: Implement audio processing
// std::optional<AudioShmBuffer::Config> ClapBridge::setup_shared_audio_buffers(
//     size_t instance_id) {
//     const auto& [instance, _] = get_instance(instance_id);

//     const Steinberg::IPtr<Steinberg::Vst::IComponent> component =
//         instance.interfaces.component;
//     const Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> audio_processor =
//         instance.interfaces.audio_processor;

//     if (!instance.process_setup || !component || !audio_processor) {
//         return std::nullopt;
//     }

//     // We'll query the plugin for its audio bus layouts, and then create
//     // calculate the offsets in a large memory buffer for the different audio
//     // channels. The offsets for each audio channel are in samples (since
//     // they'll be used with pointer arithmetic in `AudioShmBuffer`).
//     uint32_t current_offset = 0;

//     auto create_bus_offsets = [&, &setup = instance.process_setup](
//                                   Steinberg::Vst::BusDirection direction) {
//         const auto num_busses =
//             component->getBusCount(Steinberg::Vst::kAudio, direction);

//         // This function is also run from `IAudioProcessor::setActive()`.
//         // According to the docs this does not need to be realtime-safe, but we
//         // should at least still try to not do anything expensive when no work
//         // needs to be done.
//         llvm::SmallVector<llvm::SmallVector<uint32_t, 32>, 16> bus_offsets(
//             num_busses);
//         for (int bus = 0; bus < num_busses; bus++) {
//             Steinberg::Vst::SpeakerArrangement speaker_arrangement{};
//             audio_processor->getBusArrangement(direction, bus,
//                                                speaker_arrangement);

//             const size_t num_channels =
//                 std::bitset<sizeof(Steinberg::Vst::SpeakerArrangement) * 8>(
//                     speaker_arrangement)
//                     .count();
//             bus_offsets[bus].resize(num_channels);

//             for (size_t channel = 0; channel < num_channels; channel++) {
//                 bus_offsets[bus][channel] = current_offset;
//                 current_offset += setup->maxSamplesPerBlock;
//             }
//         }

//         return bus_offsets;
//     };

//     // Creating the audio buffer offsets for every channel in every bus will
//     // advacne `current_offset` to keep pointing to the starting position for
//     // the next channel
//     const auto input_bus_offsets =
//     create_bus_offsets(Steinberg::Vst::kInput); const auto output_bus_offsets
//     = create_bus_offsets(Steinberg::Vst::kOutput);

//     // The size of the buffer is in bytes, and it will depend on whether the
//     // host is going to pass 32-bit or 64-bit audio to the plugin
//     const bool double_precision =
//         instance.process_setup->symbolicSampleSize ==
//         Steinberg::Vst::kSample64;
//     const uint32_t buffer_size =
//         current_offset * (double_precision ? sizeof(double) : sizeof(float));

//     // If this function has been called previously and the size did not change,
//     // then we should not do any work
//     if (instance.process_buffers &&
//         instance.process_buffers->config_.size == buffer_size) {
//         return std::nullopt;
//     }

//     // Because the above check should be super cheap, we'll now need to convert
//     // the stack allocated SmallVectors to regular heap vectors
//     std::vector<std::vector<uint32_t>> input_bus_offsets_vector;
//     input_bus_offsets_vector.reserve(input_bus_offsets.size());
//     for (const auto& channel_offsets : input_bus_offsets) {
//         input_bus_offsets_vector.push_back(
//             std::vector(channel_offsets.begin(), channel_offsets.end()));
//     }

//     std::vector<std::vector<uint32_t>> output_bus_offsets_vector;
//     output_bus_offsets_vector.reserve(output_bus_offsets.size());
//     for (const auto& channel_offsets : output_bus_offsets) {
//         output_bus_offsets_vector.push_back(
//             std::vector(channel_offsets.begin(), channel_offsets.end()));
//     }

//     // We'll set up these shared memory buffers on the Wine side first, and then
//     // when this request returns we'll do the same thing on the native plugin
//     // side
//     AudioShmBuffer::Config buffer_config{
//         .name = sockets_.base_dir_.filename().string() + "-" +
//                 std::to_string(instance_id),
//         .size = buffer_size,
//         .input_offsets = std::move(input_bus_offsets_vector),
//         .output_offsets = std::move(output_bus_offsets_vector)};
//     if (!instance.process_buffers) {
//         instance.process_buffers.emplace(buffer_config);
//     } else {
//         instance.process_buffers->resize(buffer_config);
//     }

//     // After setting up the shared memory buffer, we need to create a vector of
//     // channel audio pointers for every bus. These will then be assigned to the
//     // `AudioBusBuffers` objects in the `ProcessData` struct in
//     // `YaProcessData::reconstruct()` before passing the reconstructed process
//     // data to `IAudioProcessor::process()`.
//     auto set_bus_pointers =
//         [&]<std::invocable<uint32_t, uint32_t> F>(
//             std::vector<std::vector<void*>>& bus_pointers,
//             const std::vector<std::vector<uint32_t>>& bus_offsets,
//             F&& get_channel_pointer) {
//             bus_pointers.resize(bus_offsets.size());

//             for (size_t bus = 0; bus < bus_offsets.size(); bus++) {
//                 bus_pointers[bus].resize(bus_offsets[bus].size());

//                 for (size_t channel = 0; channel < bus_offsets[bus].size();
//                      channel++) {
//                     bus_pointers[bus][channel] =
//                         get_channel_pointer(bus, channel);
//                 }
//             }
//         };

//     set_bus_pointers(
//         instance.process_buffers_input_pointers,
//         instance.process_buffers->config_.input_offsets,
//         [&, &instance = instance](uint32_t bus, uint32_t channel) -> void* {
//             if (double_precision) {
//                 return instance.process_buffers->input_channel_ptr<double>(
//                     bus, channel);
//             } else {
//                 return instance.process_buffers->input_channel_ptr<float>(
//                     bus, channel);
//             }
//         });
//     set_bus_pointers(
//         instance.process_buffers_output_pointers,
//         instance.process_buffers->config_.output_offsets,
//         [&, &instance = instance](uint32_t bus, uint32_t channel) -> void* {
//             if (double_precision) {
//                 return instance.process_buffers->output_channel_ptr<double>(
//                     bus, channel);
//             } else {
//                 return instance.process_buffers->output_channel_ptr<float>(
//                     bus, channel);
//             }
//         });

//     return buffer_config;
// }

void ClapBridge::register_plugin_instance(
    const clap_plugin* plugin,
    std::unique_ptr<clap_host_proxy> host_proxy) {
    std::unique_lock lock(object_instances_mutex_);

    assert(plugin);
    assert(host_proxy);

    // This instance ID has already been generated because the host proxy has to
    // be created before the plugin instance
    const size_t instance_id = host_proxy->owner_isntance_id();
    object_instances_.emplace(
        instance_id, ClapPluginInstance(plugin, std::move(host_proxy)));

    // Every plugin instance gets its own audio thread
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

        // TODO: Listen on the socket
        // sockets_.add_audio_processor_and_listen(
        //     instance_id, socket_listening_latch,
        //     overload{
        //         [&](YaAudioProcessor::SetBusArrangements& request)
        //             -> YaAudioProcessor::SetBusArrangements::Response {
        //             const auto& [instance, _] =
        //                 get_instance(request.instance_id);

        //             // HACK: WA Production Imperfect CLAP somehow requires
        //             //       `inputs` to be a valid pointer, even if there
        //             //       are no inputs.
        //             Steinberg::Vst::SpeakerArrangement empty_arrangement =
        //                 0b00000000;

        //             return instance.interfaces.audio_processor
        //                 ->setBusArrangements(
        //                     request.num_ins > 0 ? request.inputs.data()
        //                                         : &empty_arrangement,
        //                     request.num_ins,
        //                     request.num_outs > 0 ? request.outputs.data()
        //                                          : &empty_arrangement,
        //                     request.num_outs);
        //         },
        //     });
    });

    // Wait for the new socket to be listening on before continuing. Otherwise
    // the native plugin may try to connect to it before our thread is up and
    // running.
    socket_listening_latch.get_future().wait();
}

void ClapBridge::unregister_object_instance(size_t instance_id) {
    sockets_.remove_audio_thread(instance_id);

    // Remove the instance from within the main IO context so
    // removing it doesn't interfere with the Win32 message loop
    // XXX: I don't think we have to wait for the object to be
    //      deleted most of the time, but I can imagine a situation
    //      where the plugin does a host callback triggered by a
    //      Win32 timer in between where the above closure is being
    //      executed and when the actual host application context on
    //      the plugin side gets deallocated.
    main_context_
        .run_in_context([&, instance_id]() -> void {
            std::unique_lock lock(object_instances_mutex_);
            object_instances_.erase(instance_id);
        })
        .wait();
}
