// yabridge: a Wine VST bridge
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

#include "audio-shm.h"

#include <iostream>

#include "logging/common.h"

using namespace std::literals::string_literals;

AudioShmBuffer::AudioShmBuffer(const Config& config)
    : config_(config),
      shm_fd_(shm_open(config.name.c_str(), O_RDWR | O_CREAT, 0600)) {
    if (shm_fd_ == -1) {
        throw std::system_error(
            std::error_code(errno, std::system_category()),
            "Could not create shared memory object " + config_.name);
    }

    setup_mapping();
}

AudioShmBuffer::~AudioShmBuffer() noexcept {
    // If either side drops this object then the buffer should always be
    // removed, so we'll do it on both sides to reduce the chance that we leak
    // shared memory
    if (!is_moved_) {
        munmap(shm_bytes_, config_.size);
        close(shm_fd_);
        shm_unlink(config_.name.c_str());
    }
}

AudioShmBuffer::AudioShmBuffer(AudioShmBuffer&& o) noexcept
    : config_(std::move(o.config_)),
      shm_fd_(std::move(o.shm_fd_)),
      shm_bytes_(std::move(o.shm_bytes_)),
      shm_size_(std::move(o.shm_size_)) {
    o.is_moved_ = true;
}

AudioShmBuffer& AudioShmBuffer::operator=(AudioShmBuffer&& o) noexcept {
    config_ = std::move(o.config_);
    shm_fd_ = std::move(o.shm_fd_);
    shm_bytes_ = std::move(o.shm_bytes_);
    shm_size_ = std::move(o.shm_size_);
    o.is_moved_ = true;

    return *this;
}

void AudioShmBuffer::resize(const Config& new_config) {
    if (new_config.name != config_.name) {
        throw std::invalid_argument("Expected buffer configuration for \"" +
                                    config_.name + "\", got \"" +
                                    new_config.name + "\"");
    }

    config_ = new_config;
    setup_mapping();
}

void AudioShmBuffer::setup_mapping() {
    // Apparently you get a `Resource temporarily unavailable` when calling
    // `ftruncate()` with a size of 0 on shared memory
    if (config_.size > 0) {
        // I don't think this can fail
        assert(ftruncate(shm_fd_, config_.size) == 0);

        // But this can, if the user does not have permissions to use (enough)
        // locked emmory, we'll try it without locking memory and show a big
        // obnoxious warning and try again without locking the memory.
        uint8_t* old_shm_bytes = shm_bytes_;
        shm_bytes_ = static_cast<uint8_t*>(
            old_shm_bytes
                ? mremap(old_shm_bytes, shm_size_, config_.size, MREMAP_MAYMOVE)
                : mmap(nullptr, config_.size, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_LOCKED, shm_fd_, 0));
        if (shm_bytes_ == MAP_FAILED) {
            Logger logger = Logger::create_exception_logger();

            logger.log("");
            logger.log("ERROR: Could not map shared memory. This means that");
            logger.log("       your user's memory locking limit has been");
            logger.log("       reached. Check your distro's documentation or");
            logger.log("       wiki for instructions on how to set up");
            logger.log("       realtime privileges and memlock limits.");
            logger.log("");

            // Growing into a size that we cannot lock sounds like a super rare
            // edge case, but let's handle it anyways
            if (old_shm_bytes) {
                assert(munmap(old_shm_bytes, shm_size_) == 0);
            }
            shm_bytes_ = static_cast<uint8_t*>(mmap(nullptr, config_.size,
                                                    PROT_READ | PROT_WRITE,
                                                    MAP_SHARED, shm_fd_, 0));
            if (shm_bytes_ == MAP_FAILED) {
                throw std::system_error(
                    std::error_code(errno, std::system_category()),
                    "Could not map shared memory");
            }
        }
    }

    shm_size_ = config_.size;
}
