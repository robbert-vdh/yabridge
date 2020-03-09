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

// This is a modified version of `bitsery::ext::PointerOwner` in an attempt to
// support C-style arrays with no compile time known length. This should be
// upstreamed if it works.

#include <bitsery/ext/pointer.h>

// TODO: Implement

namespace bitsery {
namespace ext {
template <template <typename> class TPtrManager,
          template <typename>
          class TPolymorphicContext,
          typename RTTI>
class MultiplePointerObjectExtensionBase
    : public pointer_utils::
          PointerObjectExtensionBase<TPtrManager, TPolymorphicContext, RTTI> {
    // explicit PointerObjectExtensionBase(
    //     PointerType ptrType = PointerType::Nullable,
    //     MemResourceBase* resource = nullptr,
    //     bool resourcePropagate = false)
    //     : _ptrType{ptrType},
    //       _resourcePropagate{resourcePropagate},
    //       _resource{resource} {}
};

template <typename RTTI>
using MultiplePointerOwnerBase =
    MultiplePointerObjectExtensionBase<pointer_details::PtrOwnerManager,
                                       PolymorphicContext,
                                       RTTI>;

using MultiplePointerOwner = MultiplePointerOwnerBase<StandardRTTI>;

}  // namespace ext

namespace traits {
template <typename T, typename RTTI>
struct ExtensionTraits<ext::MultiplePointerOwnerBase<RTTI>, T*> {
    using TValue = T;
    static constexpr bool SupportValueOverload = true;
    static constexpr bool SupportObjectOverload = true;
    // if underlying type is not polymorphic, then we can enable lambda syntax
    static constexpr bool SupportLambdaOverload =
        !RTTI::template isPolymorphic<TValue>();
};
}  // namespace traits
}  // namespace bitsery
