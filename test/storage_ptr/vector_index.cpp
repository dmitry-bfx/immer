// immer: immutable data structures for C++
// Copyright (C) 2026 OpenAI
//
// This software is distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE or copy at http://boost.org/LICENSE_1_0.txt

#include <immer/storage_ptr.hpp>
#include <immer/vector.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

class test_arena final : public immer::storage_ptr_context
{
public:
    ~test_arena() override
    {
        for (const auto& slot : slots_)
            std::free(slot.address);
    }

    void* resolve_handle(std::uint64_t bits,
                         std::size_t size,
                         std::size_t alignment) const override
    {
        const auto handle = immer::storage_ptr<void>::from_bits(bits);
        if (handle.extent_id() != extent_id || handle.slot_id() >= slots_.size())
            throw std::out_of_range{"test arena handle is outside the arena"};
        const auto& slot = slots_[handle.slot_id()];
        if (size > slot.size || alignment > slot.alignment ||
            reinterpret_cast<std::uintptr_t>(slot.address) % alignment != 0)
            throw std::invalid_argument{"test arena handle layout mismatch"};
        return slot.address;
    }

    std::uint64_t encode_address(const void* address,
                                 std::size_t size,
                                 std::size_t alignment) const override
    {
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            const auto& slot = slots_[i];
            if (slot.address != address)
                continue;
            if (size > slot.size || alignment > slot.alignment ||
                reinterpret_cast<std::uintptr_t>(address) % alignment != 0)
                throw std::invalid_argument{"test arena address layout mismatch"};
            if (i > std::numeric_limits<std::uint32_t>::max())
                throw std::overflow_error{"test arena slot ID exhausted"};
            return immer::storage_ptr<void>::from_parts(
                       extent_id, static_cast<std::uint32_t>(i))
                .bits();
        }
        throw std::out_of_range{"address is not a test arena slot"};
    }

    void* allocate_slot(std::size_t size,
                        std::size_t alignment) const override
    {
        if (size > slot_size || alignment > alignof(std::max_align_t))
            throw std::bad_alloc{};
        auto* address = std::malloc(slot_size);
        if (address == nullptr)
            throw std::bad_alloc{};
        try {
            slots_.push_back({address, slot_size, alignof(std::max_align_t)});
        } catch (...) {
            std::free(address);
            throw;
        }
        return address;
    }

private:
    static constexpr std::uint32_t extent_id = 1;
    static constexpr std::size_t slot_size = 4096;

    struct slot
    {
        void* address;
        std::size_t size;
        std::size_t alignment;
    };

    mutable std::vector<slot> slots_;
};

} // namespace

TEST_CASE("storage-backed vector indexed reads descend through branches")
{
    using vector_type = immer::vector<int, immer::storage_memory_policy>;

    test_arena arena;
    immer::storage_ptr_context_scope scope{arena};

    std::vector<int> expected(4096);
    for (std::size_t i = 0; i < expected.size(); ++i)
        expected[i] = static_cast<int>(i * 3) - 2000;

    const vector_type values{expected.begin(), expected.end()};
    REQUIRE(values.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
        CHECK(values[i] == expected[i]);
}
