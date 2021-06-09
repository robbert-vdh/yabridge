// yabridge: a Wine VST bridge
// Copyright (C) 2020-2021 Robbert van der Helm
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

#include <vector>

#ifdef __WINE__
#include "../wine-host/boost-fix.h"
#endif
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>

/**
 * A shared memory object that allows audio buffers to be shared between the
 * native plugin and the Wine plugin host. This is intended as an optimization,
 * and it is used alongside yabridge's usual socket based messages. Normally
 * audio buffers would have to be copied from the host to the native plugin,
 * sent to the Wine plugin host, and then copied to a buffer on the Wine plugin
 * host side for them to be processed by the plugin. The results then have to be
 * sent back to the native plugin, where they finally have to be copied back to
 * the host's buffers. While this wouldn't be an issue for small amounts of
 * data, it also increases the overhead of bridging plugins considerably since
 * there's not much else going on. So to prevent unnecessary copies, we'll
 * communicate the audio buffer data through shared memory objects so we can
 * reduce all of the operations described above to one copy from the host to the
 * shared memory region, and another copy from the shared memory region back to
 * the host. And since we're still using messages alongside this, we also don't
 * need any locks.
 *
 * This approach introduces a few additional moving parts that we'd rather not
 * have to deal with, but the benefits likely outweigh the costs. The buffer is
 * set up on the Wine side after the VST2 or VST3 plugin has finished preparing
 * for audio processing. The configuration (e.g. name, and dimensions) for this
 * shared memory object are then sent back to the plugin so the plugin can map
 * the same shared memory region.
 */
class AudioShmBuffer {
   public:
    /**
     * The parameters needed for creating, configuring and connecting to a
     * shared audio buffer object. This is done on the Wine plugin host. For
     * this we need to know the plugin's bus/channel configuration, whether the
     * host is going to ask the plugin to process 32-bit or 64-bit floating
     * point audio, and the maximum size of the samples per audio buffer. The
     * bus/channel configuration can be queried directly from the plugin. For
     * VST2 plugins the other information is passed before the call to
     * `effMainsChanged` through `effSetProcessPrecision` and `effSetBlockSize`,
     * which would thus need to be kept track of. For VST3 plugins this is all
     * sent as part of the `Steinberg::Vst::ProcessSetup` object.
     */
    struct Config {
        /**
         * The unique identifier for this shared memory object. The backing file
         * will be created in `/dev/shm` by the operating system.
         */
        std::string name;

        /**
         * The size of the shared memory object. This should be large enough to
         * hold all input and output buffers.
         */
        uint32_t size;

        /**
         * Offsets **in samples** within the shared memory object for an input
         * audio channel, indexed by `[bus][channel]`. For VST2 plugins the bus
         * will always be 0. This can be used later to retrieve a pointer to the
         * audio channel.
         */
        std::vector<std::vector<uint32_t>> input_offsets;
        /**
         * Offsets **in samples** within the shared memory object for an output
         * audio channel, indexed by `[bus][channel]`. For VST2 plugins the bus
         * will always be 0. This can be used later to retrieve a pointer to the
         * audio channel.
         */
        std::vector<std::vector<uint32_t>> output_offsets;

        template <typename S>
        void serialize(S& s) {
            s.text1b(name, 1024);
            s.value4b(size);
            s.container(input_offsets, 8192, [](S& s, auto& offsets) {
                s.container4b(offsets, 8192);
            });
            s.container(output_offsets, 8192, [](S& s, auto& offsets) {
                s.container4b(offsets, 8192);
            });
        }
    };

    /**
     * Connect to or create the shared memory object and map it to this
     * process's memory. The configuration is created on the Wine side using the
     * process described in `Config`'s docstring.
     */
    AudioShmBuffer(const Config& config);

    /**
     * Destroy the shared memory object. Either side dropping the object will
     * cause the object to get destroyed in an effort to avoid memory leaks
     * caused by crashing plugins or hosts.
     */
    ~AudioShmBuffer() noexcept;

    AudioShmBuffer(const AudioShmBuffer&) = delete;
    AudioShmBuffer& operator=(const AudioShmBuffer&) = delete;

    AudioShmBuffer(AudioShmBuffer&&) noexcept;
    AudioShmBuffer& operator=(AudioShmBuffer&&) = delete;

    /**
     * Get a pointer to the part of the buffer where this input audio channel is
     * stored in.
     */
    template <typename T>
    T* input_channel_ptr(const uint32_t bus, const uint32_t channel) {
        return reinterpret_cast<T*>(buffer.get_address()) +
               config.input_offsets[bus][channel];
    }

    /**
     * Get a pointer to the part of the buffer where this output audio channel
     * is stored in.
     */
    template <typename T>
    T* output_channel_ptr(const uint32_t bus, const uint32_t channel) {
        return reinterpret_cast<T*>(buffer.get_address()) +
               config.output_offsets[bus][channel];
    }

    const Config config;

   private:
    boost::interprocess::shared_memory_object shm;
    boost::interprocess::mapped_region buffer;

    bool is_moved = false;
};
