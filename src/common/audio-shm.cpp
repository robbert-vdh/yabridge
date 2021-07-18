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

#include "audio-shm.h"

AudioShmBuffer::AudioShmBuffer(const Config& config)
    : config(config),
      shm(boost::interprocess::open_or_create,
          config.name.c_str(),
          boost::interprocess::read_write) {
    setup_mapping();
}

AudioShmBuffer::~AudioShmBuffer() noexcept {
    // If either side drops this object then the buffer should always be
    // removed, so we'll do it on both sides to reduce the chance that we leak
    // shared memory
    if (!is_moved) {
        boost::interprocess::shared_memory_object::remove(config.name.c_str());
    }
}

AudioShmBuffer::AudioShmBuffer(AudioShmBuffer&& o) noexcept
    : config(std::move(o.config)),
      shm(std::move(o.shm)),
      buffer(std::move(o.buffer)) {
    o.is_moved = true;
}

AudioShmBuffer& AudioShmBuffer::operator=(AudioShmBuffer&& o) noexcept {
    config = std::move(o.config);
    shm = std::move(o.shm);
    buffer = std::move(o.buffer);
    o.is_moved = true;

    return *this;
}

void AudioShmBuffer::resize(const Config& new_config) {
    if (new_config.name != config.name) {
        throw std::invalid_argument("Expected buffer configuration for \"" +
                                    config.name + "\", got \"" +
                                    new_config.name + "\"");
    }

    config = new_config;
    setup_mapping();
}

void AudioShmBuffer::setup_mapping() {
    // Apparently you get a `Resource temporarily unavailable` when calling
    // `ftruncate()` with a size of 0 on shared memory
    if (config.size > 0) {
        shm.truncate(config.size);
        buffer = boost::interprocess::mapped_region(
            shm, boost::interprocess::read_write, 0, config.size, nullptr,
            MAP_LOCKED);
    }
}
