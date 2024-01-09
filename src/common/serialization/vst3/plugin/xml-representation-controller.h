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

#pragma once

#include <pluginterfaces/vst/ivstrepresentation.h>

#include "../../common.h"
#include "../base.h"
#include "../bstream.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IXmlRepresentationController` for serialization purposes. This
 * is instantiated as part of `Vst3PluginProxy`.
 *
 * XXX: The docs talk about standard locations for XML representation files. Do
 *      plugins actually use these representations, do they place them in the
 *      standard locations, and do hosts use them? If so we should be symlinking
 *      them in yabridgectl. See
 *      https://developer.steinberg.help/display/VST/%5B3.5.0%5D+Remote+Representation+of+Parameters+Support
 */
class YaXmlRepresentationController
    : public Steinberg::Vst::IXmlRepresentationController {
   public:
    /**
     * These are the arguments for creating a `YaXmlRepresentationController`.
     */
    struct ConstructArgs {
        ConstructArgs() noexcept;

        /**
         * Check whether an existing implementation implements
         * `IXmlRepresentationController` and read arguments from it.
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
    YaXmlRepresentationController(ConstructArgs&& args) noexcept;

    virtual ~YaXmlRepresentationController() noexcept = default;

    inline bool supported() const noexcept { return arguments_.supported; }

    /**
     * The response code and written state for a call to
     * `IXmlRepresentationController::getXmlRepresentationStream(info,
     * &stream)`.
     */
    struct GetXmlRepresentationStreamResponse {
        UniversalTResult result;
        YaBStream stream;

        template <typename S>
        void serialize(S& s) {
            s.object(result);
            s.object(stream);
        }
    };

    /**
     * Message to pass through a call to
     * `IXmlRepresentationController::getXmlRepresentationStream(info, &stream)`
     * to the Wine plugin host.
     */
    struct GetXmlRepresentationStream {
        using Response = GetXmlRepresentationStreamResponse;

        native_size_t instance_id;

        Steinberg::Vst::RepresentationInfo info;
        YaBStream stream;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.object(info);
            s.object(stream);
        }
    };

    virtual tresult PLUGIN_API
    getXmlRepresentationStream(Steinberg::Vst::RepresentationInfo& info /*in*/,
                               Steinberg::IBStream* stream /*out*/) = 0;

   protected:
    ConstructArgs arguments_;
};

#pragma GCC diagnostic pop

namespace Steinberg {
namespace Vst {
template <typename S>
void serialize(S& s, RepresentationInfo& info) {
    s.text1b(info.vendor);
    s.text1b(info.name);
    s.text1b(info.version);
    s.text1b(info.host);
}
}  // namespace Vst
}  // namespace Steinberg
