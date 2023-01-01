// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2023 Robbert van der Helm
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

#include "parameter-changes.h"

YaParameterChanges::YaParameterChanges() noexcept {FUNKNOWN_CTOR}

YaParameterChanges::~YaParameterChanges() noexcept {
    FUNKNOWN_DTOR
}

void YaParameterChanges::clear() noexcept {
    queues_.clear();
}

void YaParameterChanges::repopulate(
    Steinberg::Vst::IParameterChanges& original_queues) {
    // Copy over all parameter change queues
    const size_t num_queues = original_queues.getParameterCount();
    queues_.resize(num_queues);
    for (size_t i = 0; i < num_queues; i++) {
        queues_[i].repopulate(
            *original_queues.getParameterData(static_cast<int>(i)));
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
IMPLEMENT_FUNKNOWN_METHODS(YaParameterChanges,
                           Steinberg::Vst::IParameterChanges,
                           Steinberg::Vst::IParameterChanges::iid)
#pragma GCC diagnostic pop

size_t YaParameterChanges::num_parameters() const {
    return queues_.size();
}

void YaParameterChanges::write_back_outputs(
    Steinberg::Vst::IParameterChanges& output_queues) const {
    for (auto& queue : queues_) {
        // We don't need this, but the SDK requires us to need this
        int32 output_queue_index;
        if (Steinberg::Vst::IParamValueQueue* output_queue =
                output_queues.addParameterData(queue.parameter_id_,
                                               output_queue_index)) {
            queue.write_back_outputs(*output_queue);
        }
    }
}

int32 PLUGIN_API YaParameterChanges::getParameterCount() {
    return static_cast<int32>(queues_.size());
}

Steinberg::Vst::IParamValueQueue* PLUGIN_API
YaParameterChanges::getParameterData(int32 index) {
    if (index < static_cast<int32>(queues_.size())) {
        return &queues_[index];
    } else {
        return nullptr;
    }
}

Steinberg::Vst::IParamValueQueue* PLUGIN_API
YaParameterChanges::addParameterData(const Steinberg::Vst::ParamID& id,
                                     int32& index /*out*/) {
    index = static_cast<int32>(queues_.size());

    // Tiny hack, resizing avoids calling the constructor the second time we
    // resize the vector to the same size
    queues_.resize(queues_.size() + 1);
    queues_[index].clear_for_parameter(id);

    return &queues_[index];
}
