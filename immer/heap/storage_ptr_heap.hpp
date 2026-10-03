// immer: immutable data structures for C++
// Copyright (C) 2026 OpenAI
//
// immer is distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE or copy at http://boost.org/LICENSE_1_0.txt

#pragma once

#include <immer/storage_ptr.hpp>

#include <cstddef>
#include <new>
#include <stdexcept>

namespace immer {

/*!
 * Heap adapter that obtains arena slots from the active storage context.
 * Allocation is aligned and extent selection/reclamation remain runtime
 * responsibilities. Deallocation is advisory because persistent arenas are
 * reclaimed by tracing GC.
 */
struct storage_ptr_heap
{
    static void* allocate(std::size_t size)
    {
        return allocate(size, alignof(std::max_align_t));
    }

    static void* allocate(std::size_t size, std::size_t alignment)
    {
        auto* context = detail::active_storage_ptr_context();
        if (context == nullptr)
            throw std::logic_error{
                "storage_ptr_heap requires an active storage context"};
        return context->allocate_slot(size, alignment);
    }

    template <typename Tag, typename... Tags>
    static void* allocate(std::size_t size, Tag, Tags...)
    {
        return allocate(size);
    }

    template <typename Tag, typename... Tags>
    static void* allocate(std::size_t size,
                          std::size_t alignment,
                          Tag,
                          Tags...)
    {
        return allocate(size, alignment);
    }

    static void deallocate(std::size_t size, void* address)
    {
        auto* context = detail::active_storage_ptr_context();
        if (context != nullptr)
            context->deallocate_slot(size, address);
    }

    template <typename Tag, typename... Tags>
    static void deallocate(std::size_t size,
                           void* address,
                           Tag,
                           Tags...)
    {
        deallocate(size, address);
    }
};

} // namespace immer
