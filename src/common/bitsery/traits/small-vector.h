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

#include <bitsery/traits/core/std_defaults.h>
#include <llvm/small-vector.h>

namespace bitsery {
namespace traits {

template <typename T, unsigned N>
struct ContainerTraits<llvm::SmallVector<T, N>>
    : public StdContainer<llvm::SmallVector<T, N>, true, true> {
    // The small vector implementation needs to be contiguous for this to work
};

template <typename T, unsigned N>
struct BufferAdapterTraits<llvm::SmallVector<T, N>>
    : public StdContainerForBufferAdapter<llvm::SmallVector<T, N>> {};

// And the same extensions again for the type erased version

template <typename T>
struct ContainerTraits<llvm::SmallVectorImpl<T>>
    : public StdContainer<llvm::SmallVectorImpl<T>, true, true> {};

template <typename T>
struct BufferAdapterTraits<llvm::SmallVectorImpl<T>>
    : public StdContainerForBufferAdapter<llvm::SmallVectorImpl<T>> {};

}  // namespace traits
}  // namespace bitsery
