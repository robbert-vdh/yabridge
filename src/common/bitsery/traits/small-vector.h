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

#include <bitsery/traits/core/std_defaults.h>
#include <boost/container/detail/is_contiguous_container.hpp>
#include <boost/container/small_vector.hpp>

namespace bitsery {
namespace traits {

template <typename T, size_t N, typename Allocator>
struct ContainerTraits<boost::container::small_vector<T, N, Allocator>>
    : public StdContainer<boost::container::small_vector<T, N, Allocator>,
                          true,
                          true> {
    // Unlike `std::vector`, I'm pretty sure
    // `boost::container::small_vector<bool, N>` is contiguous. So hopefully
    // this assertion does its thing.
    static_assert(boost::container::dtl::is_contiguous_container<
                  boost::container::small_vector<T, N, Allocator>>::value);
};

template <typename T, size_t N, typename Allocator>
struct BufferAdapterTraits<boost::container::small_vector<T, N, Allocator>>
    : public StdContainerForBufferAdapter<
          boost::container::small_vector<T, N, Allocator>> {};

// And the same extensions again for the type erased version

template <typename T, typename Allocator>
struct ContainerTraits<boost::container::small_vector_base<T, Allocator>>
    : public StdContainer<boost::container::small_vector_base<T, Allocator>,
                          true,
                          true> {
    static_assert(boost::container::dtl::is_contiguous_container<
                  boost::container::small_vector_base<T, Allocator>>::value);
};

template <typename T, typename Allocator>
struct BufferAdapterTraits<boost::container::small_vector_base<T, Allocator>>
    : public StdContainerForBufferAdapter<
          boost::container::small_vector_base<T, Allocator>> {};

}  // namespace traits
}  // namespace bitsery
