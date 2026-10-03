// immer: immutable data structures for C++
// Copyright (C) 2026 OpenAI
//
// This software is distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE or copy at http://boost.org/LICENSE_1_0.txt

#pragma once

#include <immer/detail/rbts/visitor.hpp>
#include <immer/memory_policy.hpp>

namespace immer {
namespace detail {
namespace rbts {

template <typename Visitor>
struct trace_visitor : visitor_base<trace_visitor<Visitor>>
{
    using this_t = trace_visitor;

    template <typename Pos>
    static void visit_regular(Pos&& pos, Visitor& visitor)
    {
        trace_inner(std::forward<Pos>(pos), visitor);
    }

    template <typename Pos>
    static void visit_relaxed(Pos&& pos, Visitor& visitor)
    {
        trace_inner(std::forward<Pos>(pos), visitor);
    }

    template <typename Pos>
    static void visit_leaf(Pos&& pos, Visitor& visitor)
    {
        auto* node = pos.node();
        if (node == nullptr)
            return;
        using pos_t  = typename std::remove_reference<Pos>::type;
        using node_t = typename pos_t::node_t;
        visitor.visit_node(node_reference(
            node,
            std::is_same<typename node_t::memory::pointer,
                         storage_pointer_policy>{}));
        auto* values = node->leaf();
        for (count_t i = 0; i < pos.count(); ++i)
            visitor.visit_value(values[i]);
    }

private:
    template <typename Pos>
    static void trace_inner(Pos&& pos, Visitor& visitor)
    {
        auto* node = pos.node();
        if (node == nullptr)
            return;
        using pos_t  = typename std::remove_reference<Pos>::type;
        using node_t = typename pos_t::node_t;
        visitor.visit_node(node_reference(
            node,
            std::is_same<typename node_t::memory::pointer,
                         storage_pointer_policy>{}));
        auto* relaxed = node->relaxed();
        if (!node_t::embed_relaxed && relaxed)
            visitor.visit_auxiliary(node->relaxed_handle());
        pos.each(this_t{}, visitor);
    }

    template <typename NodeT>
    static auto node_reference(NodeT* node, std::true_type)
    {
        using node_ptr_t = typename NodeT::node_ptr_t;
        return node_ptr_t::from_address(node);
    }

    template <typename NodeT>
    static NodeT* node_reference(NodeT* node, std::false_type)
    {
        return node;
    }
};

} // namespace rbts
} // namespace detail
} // namespace immer
