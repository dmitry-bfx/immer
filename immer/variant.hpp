// immer: immutable data structures for C++
// Copyright (C) 2026 OpenAI
//
// This software is distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE or copy at http://boost.org/LICENSE_1_0.txt

#pragma once

#include <immer/storage_ptr.hpp>

#include <cstddef>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace immer {

template <std::size_t I>
struct in_place_index_t
{};

template <std::size_t I>
constexpr in_place_index_t<I> in_place_index{};

class bad_variant_access : public std::runtime_error
{
public:
    bad_variant_access() : std::runtime_error{"immer::variant index mismatch"}
    {}
};

namespace detail {

template <typename T, typename... Ts>
struct all_same : std::true_type
{};

template <typename T, typename U, typename... Ts>
struct all_same<T, U, Ts...>
    : std::integral_constant<bool,
                             std::is_same<T, U>::value && all_same<T, Ts...>::value>
{};

template <std::size_t I, typename... Ts>
struct variant_alternative;

template <typename T, typename... Ts>
struct variant_alternative<0, T, Ts...>
{
    using type = T;
};

template <std::size_t I, typename T, typename... Ts>
struct variant_alternative<I, T, Ts...> : variant_alternative<I - 1, Ts...>
{};

} // namespace detail

/*!
 * Immutable boxed tagged value for shared or persistent storage.
 *
 * Constructing an alternative requires an active storage_ptr_context. The
 * alternative is placed in an arena slot and this object stores only its
 * positional tag and a relocatable 64-bit handle. Boxes are reclaimed by the
 * storage runtime's tracing collector; this wrapper does not run alternative
 * destructors.
 */
template <typename... Ts>
class variant
{
    static_assert(sizeof...(Ts) != 0, "immer::variant needs an alternative");
    static_assert(sizeof...(Ts) <= 0xffffffffu,
                  "immer::variant has too many positional alternatives");

public:
    using index_type = std::uint32_t;
    static constexpr std::size_t alternative_count = sizeof...(Ts);

    template <std::size_t I>
    using alternative_type =
        typename detail::variant_alternative<I, Ts...>::type;

    variant() : variant{in_place_index<0>}
    {}

    template <std::size_t I, typename... Args>
    explicit variant(in_place_index_t<I>, Args&&... args)
        : index_{static_cast<index_type>(I)}, value_{}
    {
        static_assert(I < sizeof...(Ts), "immer::variant index is out of range");
        using value_type = alternative_type<I>;
        value_ = make_storage<value_type>(std::forward<Args>(args)...)
                     .template rebind<void>();
    }

    variant(const variant&) noexcept = default;
    variant(variant&&) noexcept = default;
    variant& operator=(const variant&) noexcept = default;
    variant& operator=(variant&&) noexcept = default;

    index_type index() const noexcept { return index_; }
    storage_ptr<void> storage_handle() const noexcept { return value_; }

    template <std::size_t I>
    bool holds_alternative() const noexcept
    {
        static_assert(I < sizeof...(Ts), "immer::variant index is out of range");
        return index_ == I;
    }

    template <std::size_t I>
    const alternative_type<I>& get() const
    {
        static_assert(I < sizeof...(Ts), "immer::variant index is out of range");
        if (index_ != I)
            throw bad_variant_access{};
        if (!value_)
            throw bad_variant_access{};
        return *value_.template rebind<const alternative_type<I>>();
    }

    template <typename Visitor>
    auto visit(Visitor&& visitor) const
        -> decltype(std::forward<Visitor>(visitor)(
            std::declval<const alternative_type<0>&>()))
    {
        using result_type = decltype(std::forward<Visitor>(visitor)(
            std::declval<const alternative_type<0>&>()));
        static_assert(detail::all_same<
                          result_type,
                          decltype(std::forward<Visitor>(visitor)(
                              std::declval<const Ts&>()))...>::value,
                      "immer::variant visitor must return one common type");
        return visit_impl<0, result_type>(
            std::forward<Visitor>(visitor),
            std::integral_constant<bool, sizeof...(Ts) == 1>{});
    }

    template <typename Visitor>
    void trace_children(Visitor& visitor) const
    {
        visitor.visit_auxiliary(value_);
        visit([&](const auto& value) { visitor.visit_value(value); });
    }

    friend bool operator==(const variant& lhs, const variant& rhs)
    {
        return lhs.index_ == rhs.index_ &&
               lhs.equal_impl<0>(rhs,
                                 std::integral_constant<
                                     bool,
                                     sizeof...(Ts) == 1>{});
    }

    friend bool operator!=(const variant& lhs, const variant& rhs)
    {
        return !(lhs == rhs);
    }

private:
    template <std::size_t I, typename Result, typename Visitor>
    Result visit_impl(Visitor&& visitor, std::true_type) const
    {
        if (index_ == I)
            return std::forward<Visitor>(visitor)(get<I>());
        throw bad_variant_access{};
    }

    template <std::size_t I, typename Result, typename Visitor>
    Result visit_impl(Visitor&& visitor, std::false_type) const
    {
        if (index_ == I)
            return std::forward<Visitor>(visitor)(get<I>());
        return visit_impl<I + 1, Result>(
            std::forward<Visitor>(visitor),
            std::integral_constant<bool, I + 2 == sizeof...(Ts)>{});
    }

    template <std::size_t I>
    bool equal_impl(const variant& rhs, std::true_type) const
    {
        return index_ == I && get<I>() == rhs.template get<I>();
    }

    template <std::size_t I>
    bool equal_impl(const variant& rhs, std::false_type) const
    {
        return index_ == I ? get<I>() == rhs.template get<I>()
                           : equal_impl<I + 1>(
                                 rhs,
                                 std::integral_constant<
                                     bool,
                                     I + 2 == sizeof...(Ts)>{});
    }

    index_type index_;
    storage_ptr<void> value_;
};

template <std::size_t I, typename... Ts>
const typename variant<Ts...>::template alternative_type<I>& get(
    const variant<Ts...>& value)
{
    return value.template get<I>();
}

template <typename T, typename... Ts>
struct variant_alternative_index;

template <typename T, typename... Ts>
struct variant_alternative_index<T, T, Ts...>
    : std::integral_constant<std::size_t, 0>
{};

template <typename T, typename U, typename... Ts>
struct variant_alternative_index<T, U, Ts...>
    : std::integral_constant<std::size_t,
                             1 + variant_alternative_index<T, Ts...>::value>
{};

template <typename T, typename... Ts>
const T& get(const variant<Ts...>& value)
{
    return value.template get<variant_alternative_index<T, Ts...>::value>();
}

} // namespace immer
