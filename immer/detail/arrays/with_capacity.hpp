//
// immer: immutable data structures for C++
// Copyright (C) 2016, 2017, 2018 Juan Pedro Bolivar Puente
//
// This software is distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE or copy at http://boost.org/LICENSE_1_0.txt
//

#pragma once

#include <immer/config.hpp>
#include <immer/detail/arrays/no_capacity.hpp>

#include <cassert>
#include <cstddef>
#include <stdexcept>

namespace immer {
namespace detail {
namespace arrays {

template <typename T, typename MemoryPolicy>
struct with_capacity
{
    using no_capacity_t = no_capacity<T, MemoryPolicy>;

    using node_t = node<T, MemoryPolicy>;
    using ptr_t  = typename MemoryPolicy::template ptr<node_t>;
    using edit_t = typename MemoryPolicy::transience_t::edit;
    using size_t = std::size_t;

    ptr_t ptr;
    size_t size;
    size_t capacity;

    static const with_capacity& empty()
    {
        return empty_impl(std::is_same<typename MemoryPolicy::pointer,
                                       storage_pointer_policy>{});
    }

private:
    static const with_capacity& empty_impl(std::false_type)
    {
        static const with_capacity empty_{node_t::make_n(1), 0, 1};
        return empty_;
    }

    static const with_capacity& empty_impl(std::true_type)
    {
        static const with_capacity empty_{ptr_t{}, 0, 0};
        return empty_;
    }

public:

    with_capacity(node_t* p, size_t s, size_t c)
        : ptr{p}
        , size{s}
        , capacity{c}
    {
    }

    with_capacity(const with_capacity& other)
        : with_capacity{other.ptr, other.size, other.capacity}
    {
        inc();
    }

    with_capacity(const no_capacity_t& other)
        : with_capacity{other.ptr, other.size, other.size}
    {
        inc();
    }

    with_capacity(with_capacity&& other)
        : with_capacity{empty()}
    {
        swap(*this, other);
    }

    with_capacity& operator=(const with_capacity& other)
    {
        auto next = other;
        swap(*this, next);
        return *this;
    }

    with_capacity& operator=(with_capacity&& other)
    {
        swap(*this, other);
        return *this;
    }

    friend void swap(with_capacity& x, with_capacity& y)
    {
        using std::swap;
        swap(x.ptr, y.ptr);
        swap(x.size, y.size);
        swap(x.capacity, y.capacity);
    }

    ~with_capacity() { dec(); }

    template <typename Visitor>
    void trace_children(Visitor& visitor) const
    {
        if (!ptr)
            return;
        visitor.visit_node(ptr);
        auto* values = ptr->data();
        for (size_t i = 0; i < size; ++i)
            visitor.visit_value(values[i]);
    }

    void inc()
    {
        inc_impl(std::is_same<typename MemoryPolicy::refcount,
                              no_refcount_policy>{});
    }

    void dec()
    {
        dec_impl(std::is_same<typename MemoryPolicy::refcount,
                              no_refcount_policy>{});
    }

    const T* data() const { return ptr ? ptr->data() : nullptr; }
    T* data() { return ptr ? ptr->data() : nullptr; }

    T* data_mut(edit_t e)
    {
        if (!ptr) {
            ptr = node_t::make_e(e, capacity);
            return data();
        }
        if (!ptr->can_mutate(e)) {
            auto p = node_t::copy_e(e, capacity, ptr, size);
            dec();
            ptr = p;
        }
        return data();
    }

    operator no_capacity_t() const
    {
        if (size == capacity) {
            inc();
            return {ptr, size};
        } else {
            return {node_t::copy_n(size, ptr, size), size};
        }
    }

    template <typename Iter,
              typename Sent,
              std::enable_if_t<is_forward_iterator_v<Iter> &&
                                   compatible_sentinel_v<Iter, Sent>,
                               bool> = true>
    static with_capacity from_range(Iter first, Sent last)
    {
        auto count = static_cast<size_t>(detail::distance(first, last));
        if (count == 0)
            return empty();
        else
            return {node_t::copy_n(count, first, last), count, count};
    }

    template <typename U>
    static with_capacity from_initializer_list(std::initializer_list<U> values)
    {
        using namespace std;
        return from_range(begin(values), end(values));
    }

    static with_capacity from_fill(size_t n, T v)
    {
        if (n == 0)
            return empty();
        return {node_t::fill_n(n, v), n, n};
    }

    template <typename Fn>
    void for_each_chunk(Fn&& fn) const
    {
        auto* first = data();
        std::forward<Fn>(fn)(first, size ? first + size : first);
    }

    template <typename Fn>
    bool for_each_chunk_p(Fn&& fn) const
    {
        auto* first = data();
        return std::forward<Fn>(fn)(first, size ? first + size : first);
    }

    const T& get(std::size_t index) const { return data()[index]; }

    const T& get_check(std::size_t index) const
    {
        if (index >= size)
            IMMER_THROW(std::out_of_range{"out of range"});
        return data()[index];
    }

    bool equals(const with_capacity& other) const
    {
        return ptr == other.ptr ||
               (size == other.size &&
                (size == 0 ||
                 std::equal(data(), data() + size, other.data())));
    }

    static size_t recommend_up(size_t sz, size_t cap)
    {
        auto max = std::numeric_limits<size_t>::max();
        return sz <= cap        ? cap
               : cap >= max / 2 ? max
                                /* otherwise */
                                : std::max(2 * cap, sz);
    }

    static size_t recommend_down(size_t sz, size_t cap)
    {
        return sz == 0        ? 1
               : sz < cap / 2 ? sz * 2
                              :
                              /* otherwise */ cap;
    }

    with_capacity push_back(T value) const
    {
        auto cap = recommend_up(size + 1, capacity);
        auto p   = ptr ? node_t::copy_n(cap, ptr, size) : node_t::make_n(cap);
        IMMER_TRY {
            new (p->data() + size) T(std::move(value));
            return {p, size + 1, cap};
        }
        IMMER_CATCH (...) {
            node_t::delete_n(p, size, cap);
            IMMER_RETHROW;
        }
    }

    void push_back_mut(edit_t e, T value)
    {
        if (ptr && ptr->can_mutate(e) && capacity > size) {
            new (data() + size) T(std::move(value));
            ++size;
        } else {
            auto cap = recommend_up(size + 1, capacity);
            auto p   = ptr ? node_t::copy_e(e, cap, ptr, size)
                           : node_t::make_e(e, cap);
            IMMER_TRY {
                new (p->data() + size) T(std::move(value));
                *this = {p, size + 1, cap};
            }
            IMMER_CATCH (...) {
                node_t::delete_n(p, size, cap);
                IMMER_RETHROW;
            }
        }
    }

    with_capacity assoc(std::size_t idx, T value) const
    {
        auto p = node_t::copy_n(capacity, ptr, size);
        IMMER_TRY {
            p->data()[idx] = std::move(value);
            return {p, size, capacity};
        }
        IMMER_CATCH (...) {
            node_t::delete_n(p, size, capacity);
            IMMER_RETHROW;
        }
    }

    void assoc_mut(edit_t e, std::size_t idx, T value)
    {
        if (ptr->can_mutate(e)) {
            data()[idx] = std::move(value);
        } else {
            auto p = node_t::copy_n(capacity, ptr, size);
            IMMER_TRY {
                p->data()[idx] = std::move(value);
                *this          = {p, size, capacity};
            }
            IMMER_CATCH (...) {
                node_t::delete_n(p, size, capacity);
                IMMER_RETHROW;
            }
        }
    }

    template <typename Fn>
    with_capacity update(std::size_t idx, Fn&& op) const
    {
        auto p = node_t::copy_n(capacity, ptr, size);
        IMMER_TRY {
            auto& elem = p->data()[idx];
            elem       = std::forward<Fn>(op)(std::move(elem));
            return {p, size, capacity};
        }
        IMMER_CATCH (...) {
            node_t::delete_n(p, size, capacity);
            IMMER_RETHROW;
        }
    }

    template <typename Fn>
    void update_mut(edit_t e, std::size_t idx, Fn&& op)
    {
        if (ptr->can_mutate(e)) {
            auto& elem = data()[idx];
            elem       = std::forward<Fn>(op)(std::move(elem));
        } else {
            auto p = node_t::copy_e(e, capacity, ptr, size);
            IMMER_TRY {
                auto& elem = p->data()[idx];
                elem       = std::forward<Fn>(op)(std::move(elem));
                *this      = {p, size, capacity};
            }
            IMMER_CATCH (...) {
                node_t::delete_n(p, size, capacity);
                IMMER_RETHROW;
            }
        }
    }

    with_capacity take(std::size_t sz) const
    {
        assert(sz <= size);
        auto cap = recommend_down(sz, capacity);
        auto p   = ptr ? node_t::copy_n(cap, ptr, sz) : node_t::make_n(cap);
        return {p, sz, cap};
    }

    void take_mut(edit_t e, std::size_t sz)
    {
        assert(sz <= size);
        if (ptr && ptr->can_mutate(e)) {
            detail::destroy_n(data() + size, size - sz);
            size = sz;
        } else {
            auto cap = recommend_down(sz, capacity);
            auto p   = ptr ? node_t::copy_e(e, cap, ptr, sz)
                           : node_t::make_e(e, cap);
            *this    = {p, sz, cap};
        }
    }

    node_t* native_ptr() const { return ptr; }

private:
    void inc_impl(std::true_type) {}
    void inc_impl(std::false_type)
    {
        if (ptr)
            ptr->refs().inc();
    }

    void dec_impl(std::true_type) {}
    void dec_impl(std::false_type)
    {
        if (ptr && ptr->refs().dec())
            node_t::delete_n(ptr, size, capacity);
    }
};

} // namespace arrays
} // namespace detail
} // namespace immer
