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

#include <pluginterfaces/vst/ivstparameterchanges.h>
#include <boost/container/small_vector.hpp>

#include "../../bitsery/traits/small-vector.h"
#include "base.h"
#include "param-value-queue.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IParameterChanges` for serialization purposes. Used in
 * `YaProcessData`.
 */
class YaParameterChanges : public Steinberg::Vst::IParameterChanges {
   public:
    /**
     * We only provide a default constructor here, because we need to fill the
     * existing object with new data every processing cycle to avoid
     * reallocating a new object every time.
     */
    YaParameterChanges() noexcept;

    ~YaParameterChanges() noexcept;

    /**
     * Remove all parameter changes. Used when a null pointer gets passed to the
     * input parameters field, and so the plugin can output its own parameter
     * changes.
     */
    void clear() noexcept;

    /**
     * Read data from an `IParameterChanges` object into this existing object.
     */
    void repopulate(Steinberg::Vst::IParameterChanges& original_queues);

    DECLARE_FUNKNOWN_METHODS

    /**
     * Return the number of parameter we have parameter change queues for. Used
     * in debug logs.
     */
    size_t num_parameters() const;

    /**
     * Write these changes back to an output parameter changes queue on the
     * `ProcessData` object provided by the host.
     */
    void write_back_outputs(
        Steinberg::Vst::IParameterChanges& output_queues) const;

    // From `IParameterChanges`
    int32 PLUGIN_API getParameterCount() override;
    Steinberg::Vst::IParamValueQueue* PLUGIN_API
    getParameterData(int32 index) override;
    Steinberg::Vst::IParamValueQueue* PLUGIN_API
    addParameterData(const Steinberg::Vst::ParamID& id,
                     int32& index /*out*/) override;

    template <typename S>
    void serialize(S& s) {
        s.container(queues, 1 << 16);
    }

   private:
    /**
     * The parameter value changes queues.
     */
    boost::container::small_vector<YaParamValueQueue, 16> queues;
};

#pragma GCC diagnostic pop
