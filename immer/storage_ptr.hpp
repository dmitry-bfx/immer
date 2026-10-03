// immer: immutable data structures for C++
// Copyright (C) 2026 OpenAI
//
// This software is distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE or copy at http://boost.org/LICENSE_1_0.txt

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace immer {

//! Process-local resolver installed while a shared snapshot is pinned.
class storage_ptr_context
{
public:
    virtual ~storage_ptr_context() = default;
    virtual void* resolve_handle(std::uint64_t bits,
                                 std::size_t size,
                                 std::size_t alignment) const = 0;
    virtual std::uint64_t encode_address(const void* address,
                                         std::size_t size,
                                         std::size_t alignment) const = 0;
    virtual void* allocate_slot(std::size_t, std::size_t) const
    {
        throw std::logic_error{
            "active storage context does not provide an arena allocator"};
    }
    virtual void deallocate_slot(std::size_t, void*) const {}
};

namespace detail {

template <typename T, bool = std::is_void<typename std::remove_cv<T>::type>::value>
struct storage_ptr_type_layout
{
    static constexpr std::size_t size = sizeof(T);
    static constexpr std::size_t alignment = alignof(T);
};

template <typename T>
struct storage_ptr_type_layout<T, true>
{
    static constexpr std::size_t size = 0;
    static constexpr std::size_t alignment = 1;
};

inline const storage_ptr_context*& active_storage_ptr_context() noexcept
{
    static thread_local const storage_ptr_context* context = nullptr;
    return context;
}

} // namespace detail

//! Installs a pinned mapping directory for the lifetime of one read scope.
class storage_ptr_context_scope
{
public:
    explicit storage_ptr_context_scope(
        const storage_ptr_context& context) noexcept
        : previous_{detail::active_storage_ptr_context()}
    {
        detail::active_storage_ptr_context() = &context;
    }

    ~storage_ptr_context_scope()
    {
        detail::active_storage_ptr_context() = previous_;
    }

    storage_ptr_context_scope(const storage_ptr_context_scope&) = delete;
    storage_ptr_context_scope& operator=(const storage_ptr_context_scope&) =
        delete;

private:
    const storage_ptr_context* previous_;
};

/*!
 * A typed view of a relocatable storage handle.
 *
 * The handle deliberately contains no process address.  Its upper 32 bits
 * identify an extent and its lower 32 bits identify a slot in that extent.
 * Extent zero is reserved, making an all-zero handle the null value.
 * Resolving a handle requires an active storage_ptr_context, normally
 * installed by a pinned Lager snapshot. The context is process-local; it is
 * never part of the 64-bit representation.
 */
template <typename T>
class storage_ptr
{
public:
    using element_type = T;
    using bits_type    = std::uint64_t;

    storage_ptr() noexcept = default;
    storage_ptr(T* address) : bits_{from_address(address).bits_} {}

    static constexpr storage_ptr from_bits(bits_type bits) noexcept
    {
        return storage_ptr{bits, private_tag{}};
    }

    static storage_ptr from_address(T* address)
    {
        if (address == nullptr)
            return {};
        auto* context = detail::active_storage_ptr_context();
        if (context == nullptr)
            throw std::logic_error{
                "storage_ptr::from_address requires an active storage context"};
        return from_bits(context->encode_address(
            address,
            detail::storage_ptr_type_layout<T>::size,
            detail::storage_ptr_type_layout<T>::alignment));
    }

    static constexpr storage_ptr from_parts(std::uint32_t extent,
                                            std::uint32_t slot) noexcept
    {
        return from_bits((static_cast<bits_type>(extent) << 32) | slot);
    }

    constexpr bits_type bits() const noexcept { return bits_; }
    constexpr std::uint32_t extent_id() const noexcept
    {
        return static_cast<std::uint32_t>(bits_ >> 32);
    }
    constexpr std::uint32_t slot_id() const noexcept
    {
        return static_cast<std::uint32_t>(bits_);
    }
    constexpr explicit operator bool() const noexcept { return bits_ != 0; }

    T* get() const
    {
        if (!*this)
            return nullptr;
        auto* context = detail::active_storage_ptr_context();
        if (context == nullptr)
            throw std::logic_error{
                "storage_ptr::get requires an active storage context"};
        return static_cast<T*>(context->resolve_handle(
            bits_,
            detail::storage_ptr_type_layout<T>::size,
            detail::storage_ptr_type_layout<T>::alignment));
    }

    template <typename U = T,
              typename std::enable_if<!std::is_void<U>::value, int>::type = 0>
    U* operator->() const
    {
        return get();
    }

    template <typename U = T,
              typename std::enable_if<!std::is_void<U>::value, int>::type = 0>
    U& operator*() const
    {
        return *get();
    }

    operator T*() const { return get(); }

    template <typename U>
    constexpr storage_ptr<U> rebind() const noexcept
    {
        return storage_ptr<U>::from_bits(bits_);
    }

    friend constexpr bool operator==(storage_ptr lhs,
                                     storage_ptr rhs) noexcept
    {
        return lhs.bits_ == rhs.bits_;
    }
    friend constexpr bool operator!=(storage_ptr lhs,
                                     storage_ptr rhs) noexcept
    {
        return !(lhs == rhs);
    }

private:
    struct private_tag
    {};
    constexpr storage_ptr(bits_type bits, private_tag) noexcept : bits_{bits} {}

    bits_type bits_;
};

//! Atomic publication form for handles on targets with lock-free 64-bit atomics.
template <typename T>
using atomic_storage_ptr = std::atomic<storage_ptr<T>>;

static_assert(sizeof(atomic_storage_ptr<void>) == sizeof(std::uint64_t),
              "atomic storage handles must occupy one machine word");

//! Query whether the target can publish shared handles without a lock.
inline bool storage_ptr_atomics_lock_free() noexcept
{
    std::atomic<storage_ptr<void>> value;
    return value.is_lock_free();
}

/*!
 * Construct a typed object in the active storage arena and return its
 * relocatable handle. The storage runtime owns reclamation; this helper does
 * not arrange for the object's destructor to run.
 */
template <typename T, typename... Args>
storage_ptr<T> make_storage(Args&&... args)
{
    auto* context = detail::active_storage_ptr_context();
    if (context == nullptr)
        throw std::logic_error{
            "immer::make_storage requires an active storage context"};
    auto* memory = context->allocate_slot(sizeof(T), alignof(T));
    if (memory == nullptr)
        throw std::bad_alloc{};
    auto* object = ::new (memory) T(std::forward<Args>(args)...);
    return storage_ptr<T>::from_address(object);
}

static_assert(sizeof(storage_ptr<void>) == sizeof(std::uint64_t),
              "storage_ptr must remain a single 64-bit word");
static_assert(std::is_trivially_copyable<storage_ptr<void>>::value,
              "storage_ptr must be trivially copyable");
static_assert(std::is_trivially_default_constructible<storage_ptr<void>>::value,
              "storage_ptr must remain trivially default constructible");

} // namespace immer
