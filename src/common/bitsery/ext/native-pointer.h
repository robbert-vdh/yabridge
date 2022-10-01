// yabridge: a Wine plugin bridge
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

#include <ghc/filesystem.hpp>

#include <bitsery/details/serialization_common.h>
#include <bitsery/traits/core/traits.h>

#include "../../serialization/common.h"

namespace bitsery {
namespace ext {

/**
 * An adapter for serializing and deserializing native pointer types. This makes
 * it possible to serialize `void*` fields in CLAP structs as a `native_size_t`
 * without having to modify the struct. Used in the CLAP event serialization.
 */
class NativePointer {
   public:
    template <typename Ser, typename Fnc>
    void serialize(Ser& ser, const void* const& pointer, Fnc&&) const {
        const auto native_pointer =
            static_cast<native_size_t>(reinterpret_cast<size_t>(pointer));
        ser.value8b(native_pointer);
    }

    template <typename Des, typename Fnc>
    void deserialize(Des& des, void*& pointer, Fnc&&) const {
        native_size_t native_pointer;
        des.value8b(native_pointer);
        pointer = reinterpret_cast<void*>(static_cast<size_t>(native_pointer));
    }
};

}  // namespace ext

namespace traits {

template <>
struct ExtensionTraits<ext::NativePointer, void*> {
    using TValue = void;
    static constexpr bool SupportValueOverload = false;
    static constexpr bool SupportObjectOverload = true;
    static constexpr bool SupportLambdaOverload = false;
};

}  // namespace traits
}  // namespace bitsery
