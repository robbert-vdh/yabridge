// yabridge: a Wine VST bridge
// Copyright (C) 2020  Robbert van der Helm
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

#include <bitsery/traits/vector.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>

#include "base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IParamValueQueue` for serializing a queue containing changes to
 * a single parameter during the current processing cycle. Used in
 * `YaParameterChanges`.
 */
class YaParamValueQueue : public Steinberg::Vst::IParamValueQueue {
   public:
    /**
     * Default constructor with an empty queue.
     */
    YaParamValueQueue();

    /**
     * Read data from an existing `IParamValueQueue` object.
     */
    YaParamValueQueue(Steinberg::Vst::IParamValueQueue&);

    ~YaParamValueQueue();

    DECLARE_FUNKNOWN_METHODS

    // From `IParamValueQueue`
    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override;
    int32 PLUGIN_API getPointCount() override;
    tresult PLUGIN_API
    getPoint(int32 index,
             int32& sampleOffset /*out*/,
             Steinberg::Vst::ParamValue& value /*out*/) override;
    tresult PLUGIN_API addPoint(int32 sampleOffset,
                                Steinberg::Vst::ParamValue value,
                                int32& index /*out*/) override;

    template <typename S>
    void serialize(S& s) {
        s.value4b(parameter_id);
        // TODO: Does bitsery have a built in way to serialize pairs?
        s.container(queue, 1 << 16, [](S& s, std::pair<int32, double>& pair) {
            s.value4b(pair.first);
            s.value8b(pair.second);
        });
    }

   private:
    /**
     * For `IParamValueQueue::getParameterId`.
     */
    Steinberg::Vst::ParamID parameter_id;

    /**
     * The actual parameter changes queue. The specification doesn't mention
     * that this should be a priority queue or something, but I'd assume both
     * the plugin and the host will insert the values in chronological order
     * (because, why would they not?).
     *
     * This contains pairs of `(sample_offset, value)`.
     */
    std::vector<std::pair<int32, Steinberg::Vst::ParamValue>> queue;
};

#pragma GCC diagnostic pop
