
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

#pragma once

#include <optional>

#include <bitsery/details/serialization_common.h>
#include <bitsery/traits/core/traits.h>

#include "../../serialization/common.h"

namespace bitsery {
namespace ext {

/**
 * An adapter for serializing zero-copy references to objects using
 * `MessageHandler<T>`. The idea is that when serializing, we just read data
 * from the object pointed at by the reference. Then when deserializing, we'll
 * write the data to some backing `std::option<T>` (so we don't have to
 * initialize an unused object on the serializing side), and we'll then change
 * our reference to point to the value contained within that option.
 *
 * This lets us serialize 'references' to objects that can be backed by actual
 * persistent objects. That way we can avoid allocations during the processing
 * loop.
 */
template <typename T>
class MessageReference {
   public:
    /**
     * @param backing_object The object we'll deserialize into, so we can point
     *   the `MessageReference<T>` to this object. On the serializing side this
     *   won't be touched.
     */
    MessageReference(std::optional<T>& backing_object)
        : backing_object_(backing_object){};

    template <typename Ser, typename Fnc>
    void serialize(Ser& ser,
                   const ::MessageReference<T>& object_ref,
                   Fnc&&) const {
        ser.object(object_ref.get());
    }

    template <typename Des, typename Fnc>
    void deserialize(Des& des, ::MessageReference<T>& object_ref, Fnc&&) const {
        if (!backing_object_) {
            backing_object_.emplace();
        }

        // Since we cannot directly deserialize into a reference, we'll
        // deserialize into this (persistent) backing object and then point the
        // reference to this object.
        des.object(*backing_object_);
        object_ref = *backing_object_;
    }

   private:
    /**
     * This contains the actual `T` we'll deserialize into so we can point the
     * reference to that object after deserializing.
     */
    std::optional<T>& backing_object_;
};

}  // namespace ext

namespace traits {

template <typename T>
struct ExtensionTraits<ext::MessageReference<T>, ::MessageReference<T>> {
    using TValue = void;
    static constexpr bool SupportValueOverload = false;
    static constexpr bool SupportObjectOverload = true;
    static constexpr bool SupportLambdaOverload = false;
};

}  // namespace traits
}  // namespace bitsery
