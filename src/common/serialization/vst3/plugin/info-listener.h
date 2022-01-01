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

#pragma once

#include <pluginterfaces/vst/ivstchannelcontextinfo.h>

#include "../../common.h"
#include "../attribute-list.h"
#include "../base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IInfoListener` for serialization purposes. This is instantiated
 * as part of `Vst3PluginProxy`.
 */
class YaInfoListener : public Steinberg::Vst::ChannelContext::IInfoListener {
   public:
    /**
     * These are the arguments for creating a `YaInfoListener`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements `IInfoListener`
         * and read arguments from it.
         */
        ConstructArgs(Steinberg::IPtr<Steinberg::FUnknown> object) noexcept;

        /**
         * Whether the object supported this interface.
         */
        bool supported;

        template <typename S>
        void serialize(S& s) {
            s.value1b(supported);
        }
    };

    /**
     * Instantiate this instance with arguments read from another interface
     * implementation.
     */
    YaInfoListener(ConstructArgs&& args) noexcept;

    inline bool supported() const noexcept { return arguments_.supported; }

    /**
     * Message to pass through a call to
     * `IInfoListeener::setChannelContextInfos(list)` to the Wine plugin host.
     */
    struct SetChannelContextInfos {
        using Response = UniversalTResult;

        native_size_t instance_id;

        /**
         * The passed channel context attributes, read using
         * `YaAttributeList::read_channel_context()`.
         */
        YaAttributeList list;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.object(list);
        }
    };

    virtual tresult PLUGIN_API
    setChannelContextInfos(Steinberg::Vst::IAttributeList* list) override = 0;

   protected:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop
