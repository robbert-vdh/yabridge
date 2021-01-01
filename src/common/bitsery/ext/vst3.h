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

#include <pluginterfaces/base/funknown.h>

#include <bitsery/details/serialization_common.h>
#include <bitsery/traits/core/traits.h>
#include <bitsery/traits/string.h>

namespace bitsery {
namespace ext {

/**
 * An adapter for serializing and deserializing `Steinberg::FUID`s.
 */
class FUID {
   public:
    template <typename Ser, typename Fnc>
    void serialize(Ser& ser, const Steinberg::FUID& uid, Fnc&&) const {
        Steinberg::FUID::String uid_str;
        uid.toString(uid_str);

        ser.text1b(uid_str);
    }

    template <typename Des, typename Fnc>
    void deserialize(Des& des, Steinberg::FUID& uid, Fnc&&) const {
        Steinberg::FUID::String uid_str;
        des.text1b(uid_str);

        uid.fromString(uid_str);
    }
};

}  // namespace ext

namespace traits {

template <>
struct ExtensionTraits<ext::FUID, Steinberg::FUID> {
    using TValue = void;
    static constexpr bool SupportValueOverload = false;
    static constexpr bool SupportObjectOverload = true;
    static constexpr bool SupportLambdaOverload = false;
};

}  // namespace traits
}  // namespace bitsery
