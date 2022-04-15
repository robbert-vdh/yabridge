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

#include <bitsery/ext/std_variant.h>

namespace bitsery {
namespace ext {

/**
 * A temporary replacement for `bitsery::ext::StdVariant` to avoid
 * reinitializing the object we're deserializing into if the requested variant
 * is currently active. For storing audio buffers we use variants containing
 * float and double vectors to have a type safe way to disambiguate between
 * single and double precision audio, but as it turns out bitsery's
 * `std::variant` extension would always reinitialize those objects, undoing our
 * efforts to prevent allocations.
 */
template <typename... Overloads>
class InPlaceVariant : public StdVariant<Overloads...> {
   public:
    template <typename Des, typename Fnc, typename... Ts>
    void deserialize(Des& des, std::variant<Ts...>& obj, Fnc&&) const {
        size_t index{};
        details::readSize(
            des.adapter(), index, sizeof...(Ts),
            std::integral_constant<bool, Des::TConfig::CheckDataErrors>{});

        // Most of this is copied directly from the original implementation.
        // We just added the check here to reuse the existing object if
        // possible.
        this->execIndex(index, obj, [this, &des](auto& data, auto index) {
            constexpr size_t Index = decltype(index)::value;
            using TElem =
                typename std::variant_alternative<Index,
                                                  std::variant<Ts...>>::type;
            // Reinitializing nontrivial types may be expensive especially when
            // they reference heap data, so if `data` is already holding the
            // requested variant then we'll deserialize into the existing object
            if constexpr (!std::is_trivial_v<TElem>) {
                if (auto item = std::get_if<TElem>(&data)) {
                    this->serializeType(des, *item);
                    return;
                }
            }

            TElem item = ::bitsery::Access::create<TElem>();
            this->serializeType(des, item);
            data = std::variant<Ts...>(std::in_place_index_t<Index>{},
                                       std::move(item));
        });
    }
};

template <typename... Overloads>
InPlaceVariant(Overloads...) -> InPlaceVariant<Overloads...>;
}  // namespace ext

namespace traits {

template <typename Variant, typename... Overloads>
struct ExtensionTraits<ext::InPlaceVariant<Overloads...>, Variant> {
    static_assert(
        bitsery::details::IsSpecializationOf<Variant, std::variant>::value,
        "InPlaceVariant only works with std::variant");
    using TValue = void;
    static constexpr bool SupportValueOverload = false;
    static constexpr bool SupportObjectOverload = true;
    static constexpr bool SupportLambdaOverload = false;
};

}  // namespace traits
}  // namespace bitsery
