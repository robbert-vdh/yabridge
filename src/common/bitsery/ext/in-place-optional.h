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

#include <bitsery/ext/std_optional.h>

namespace bitsery {
namespace ext {

/**
 * A temporary replacement for `bitsery::ext::InPlaceOptional` to avoid
 * reinitializing the object we're deserializing into if it already holds a
 * value. This follows the same idea as our `InPLaceVariant`.
 *
 * This is copied almost verbatim from `bitsery::ext::InPlaceOptional` (we can't
 * access the private member, so we couldn't override just the deserialization
 * method).
 */
class InPlaceOptional {
   public:
    /**
     * Works with std::optional types
     * @param alignBeforeData only makes sense when bit-packing enabled, by
     * default aligns after writing/reading bool state of optional
     */
    explicit InPlaceOptional(bool alignBeforeData = true)
        : _alignBeforeData{alignBeforeData} {}

    template <typename Ser, typename T, typename Fnc>
    void serialize(Ser& ser, const std::optional<T>& obj, Fnc&& fnc) const {
        ser.boolValue(static_cast<bool>(obj));
        if (_alignBeforeData)
            ser.adapter().align();
        if (obj)
            fnc(ser, const_cast<T&>(*obj));
    }

    template <typename Des, typename T, typename Fnc>
    void deserialize(Des& des, std::optional<T>& obj, Fnc&& fnc) const {
        bool exists{};
        des.boolValue(exists);
        if (_alignBeforeData)
            des.adapter().align();
        if (exists) {
            // Reinitializing nontrivial types may be expensive
            // especially when they reference heap data, so if `obj`
            // already holds a value then we'll deserialize into the
            // existing object
            if constexpr (!std::is_trivial_v<T>) {
                if (obj) {
                    fnc(des, *obj);
                    return;
                }
            }

            obj = ::bitsery::Access::create<T>();
            fnc(des, *obj);
        } else {
            obj = std::nullopt;
        }
    }

   private:
    bool _alignBeforeData;
};
}  // namespace ext

namespace traits {
template <typename T>
struct ExtensionTraits<ext::InPlaceOptional, std::optional<T>> {
    using TValue = T;
    static constexpr bool SupportValueOverload = true;
    static constexpr bool SupportObjectOverload = true;
    static constexpr bool SupportLambdaOverload = true;
};
}  // namespace traits
}  // namespace bitsery
