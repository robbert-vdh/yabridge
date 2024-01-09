// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2024 Robbert van der Helm
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

#include "param-value-queue.h"

YaParamValueQueue::YaParamValueQueue() noexcept {FUNKNOWN_CTOR}

YaParamValueQueue::~YaParamValueQueue() noexcept {
    FUNKNOWN_DTOR
}

void YaParamValueQueue::clear_for_parameter(
    Steinberg::Vst::ParamID parameter_id) noexcept {
    parameter_id_ = parameter_id;
    queue_.clear();
}

void YaParamValueQueue::repopulate(
    Steinberg::Vst::IParamValueQueue& original_queue) {
    parameter_id_ = original_queue.getParameterId();

    // Copy over all points to our vector
    queue_.resize(original_queue.getPointCount());
    for (int i = 0; i < original_queue.getPointCount(); i++) {
        // We're skipping the assertions here and just assume that the function
        // returns `kResultOk`
        original_queue.getPoint(i, queue_[i].first, queue_[i].second);
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
IMPLEMENT_FUNKNOWN_METHODS(YaParamValueQueue,
                           Steinberg::Vst::IParamValueQueue,
                           Steinberg::Vst::IParamValueQueue::iid)
#pragma GCC diagnostic pop

void YaParamValueQueue::write_back_outputs(
    Steinberg::Vst::IParamValueQueue& output_queue) const {
    // We don't need this value
    int32 index;
    for (const auto& [sample_offset, value] : queue_) {
        // We don't check for `kResultOk` here
        output_queue.addPoint(sample_offset, value, index);
    }
}

Steinberg::Vst::ParamID PLUGIN_API YaParamValueQueue::getParameterId() {
    return parameter_id_;
}

int32 PLUGIN_API YaParamValueQueue::getPointCount() {
    return static_cast<int32>(queue_.size());
}

tresult PLUGIN_API YaParamValueQueue::getPoint(
    int32 index,
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    int32& sampleOffset /*out*/,
    Steinberg::Vst::ParamValue& value /*out*/) {
    // Indices are signed integers, fun
    if (index >= 0 && index < static_cast<int32>(queue_.size())) {
        sampleOffset = queue_[index].first;
        value = queue_[index].second;

        return Steinberg::kResultOk;
    } else {
        return Steinberg::kInvalidArgument;
    }
}
tresult PLUGIN_API YaParamValueQueue::addPoint(int32 sampleOffset,
                                               Steinberg::Vst::ParamValue value,
                                               int32& index /*out*/) {
    index = static_cast<int32>(queue_.size());
    queue_.push_back({sampleOffset, value});

    return Steinberg::kResultOk;
}
