#include <algorithm>
#include <boost/ut.hpp>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <fieldpack/detail/aligned_allocator.hpp>
#include <fieldpack/detail/field_storage.hpp>
#include <limits>
#include <memory>
#include <new>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

/**
 * Test-only raw allocation policy used to observe ownership and inject a
 * deterministic allocation failure. Production code must not depend on it.
 */
struct tracking_allocation_policy {
    static inline std::size_t allocation_attempts{};
    static inline std::size_t successful_allocations{};
    static inline std::size_t deallocations{};
    static inline std::size_t live_allocations{};
    static inline std::size_t fail_on_attempt{};

    static void reset() noexcept
    {
        allocation_attempts = 0;
        successful_allocations = 0;
        deallocations = 0;
        live_allocations = 0;
        fail_on_attempt = 0;
    }

    [[nodiscard]] static auto allocate_bytes(std::size_t bytes, std::size_t alignment) -> void*
    {
        ++allocation_attempts;
        if (fail_on_attempt != 0 && allocation_attempts == fail_on_attempt) {
            throw std::bad_alloc{};
        }

        auto* allocation = ::operator new(bytes, std::align_val_t{alignment});
        ++successful_allocations;
        ++live_allocations;
        return allocation;
    }

    static void deallocate_bytes(void* allocation, std::size_t bytes, std::size_t alignment) noexcept
    {
        if (allocation == nullptr) {
            return;
        }

        ::operator delete(allocation, bytes, std::align_val_t{alignment});
        ++deallocations;
        --live_allocations;
    }
};

template<class T, std::size_t Alignment>
concept has_aligned_allocator = requires { typename fieldpack::detail::aligned_allocator<T, Alignment>; };

static_assert(fieldpack::detail::default_alignment == 64);
static_assert(has_aligned_allocator<float, fieldpack::detail::default_alignment>);
static_assert(has_aligned_allocator<double, fieldpack::detail::default_alignment>);
static_assert(!has_aligned_allocator<float, 0>);
static_assert(!has_aligned_allocator<float, 3>);

using float_allocator = fieldpack::detail::aligned_allocator<float>;
using rebound_double_allocator = std::allocator_traits<float_allocator>::template rebind_alloc<double>;

static_assert(std::same_as<float_allocator::value_type, float>);
static_assert(std::same_as<rebound_double_allocator, fieldpack::detail::aligned_allocator<double>>);
static_assert(std::allocator_traits<float_allocator>::is_always_equal::value);
static_assert(std::equality_comparable<float_allocator>);

template<class T> void check_aligned_allocation()
{
    using allocator_type = fieldpack::detail::aligned_allocator<T>;

    allocator_type allocator;
    constexpr std::size_t count = 7;
    auto* allocation = allocator.allocate(count);

    boost::ut::expect(allocation != nullptr);
    boost::ut::expect(reinterpret_cast<std::uintptr_t>(allocation) % fieldpack::detail::default_alignment == 0U);

    allocator.deallocate(allocation, count);
}

template<class T> void check_value_initialized_field_storage()
{
    constexpr std::size_t count = 13;
    fieldpack::detail::field_storage<T> storage(count);

    static_assert(std::ranges::contiguous_range<decltype(storage)>);
    static_assert(std::same_as<std::ranges::range_value_t<decltype(storage)>, T>);

    boost::ut::expect(storage.size() == count);
    boost::ut::expect(std::ranges::distance(storage) == static_cast<std::ptrdiff_t>(count));
    boost::ut::expect(std::to_address(storage.begin()) == storage.data());
    boost::ut::expect(std::to_address(storage.end()) == storage.data() + count);
    boost::ut::expect(std::ranges::all_of(storage, [](T value) { return value == T{}; }));
    boost::ut::expect(reinterpret_cast<std::uintptr_t>(storage.data()) % fieldpack::detail::default_alignment == 0U);
}

template<class Function> void for_each_supported_arithmetic_type(Function&& function)
{
    function.template operator()<char>();
    function.template operator()<signed char>();
    function.template operator()<unsigned char>();
    function.template operator()<wchar_t>();
    function.template operator()<char8_t>();
    function.template operator()<char16_t>();
    function.template operator()<char32_t>();
    function.template operator()<short>();
    function.template operator()<unsigned short>();
    function.template operator()<int>();
    function.template operator()<unsigned int>();
    function.template operator()<long>();
    function.template operator()<unsigned long>();
    function.template operator()<long long>();
    function.template operator()<unsigned long long>();
    function.template operator()<float>();
    function.template operator()<double>();
    function.template operator()<long double>();
}

} // namespace

int main()
{
    using namespace boost::ut;

    "zero-sized allocation is deterministic and does not reach the policy"_test = [] {
        using allocator_type =
            fieldpack::detail::aligned_allocator<int, fieldpack::detail::default_alignment, tracking_allocation_policy>;

        tracking_allocation_policy::reset();
        allocator_type allocator;

        auto* allocation = allocator.allocate(0);
        expect(allocation == nullptr);
        expect(tracking_allocation_policy::allocation_attempts == 0U);

        allocator.deallocate(allocation, 0);
        allocator.deallocate(nullptr, 0);
        expect(tracking_allocation_policy::deallocations == 0U);
        expect(tracking_allocation_policy::live_allocations == 0U);
    };

    "allocations of every supported arithmetic type are 64-byte aligned"_test = [] {
        for_each_supported_arithmetic_type([]<class T> { check_aligned_allocation<T>(); });
    };

    "allocation-size multiplication overflow is rejected"_test = [] {
        fieldpack::detail::aligned_allocator<double> allocator;
        constexpr auto overflowing_count = std::numeric_limits<std::size_t>::max() / sizeof(double) + 1U;

        expect(throws<std::bad_array_new_length>([&] { static_cast<void>(allocator.allocate(overflowing_count)); }));
    };

    "an injected allocation failure leaves no allocation behind"_test = [] {
        using storage_type = fieldpack::detail::field_storage<int, tracking_allocation_policy>;

        tracking_allocation_policy::reset();
        tracking_allocation_policy::fail_on_attempt = 1;

        expect(throws<std::bad_alloc>([] { storage_type storage(8); }));
        expect(tracking_allocation_policy::successful_allocations == 0U);
        expect(tracking_allocation_policy::deallocations == 0U);
        expect(tracking_allocation_policy::live_allocations == 0U);
    };

    "a failed growth preserves owned storage and destruction releases it"_test = [] {
        using storage_type = fieldpack::detail::field_storage<int, tracking_allocation_policy>;

        tracking_allocation_policy::reset();
        {
            storage_type storage(4, 17);
            auto* original_data = storage.data();

            expect(tracking_allocation_policy::live_allocations == 1U);
            tracking_allocation_policy::fail_on_attempt = 2;

            expect(throws<std::bad_alloc>([&] { storage.reserve(storage.capacity() + 1U); }));
            expect(storage.data() == original_data);
            expect(storage.size() == 4U);
            expect(std::ranges::all_of(storage, [](int value) { return value == 17; }));
            expect(tracking_allocation_policy::live_allocations == 1U);
        }

        expect(tracking_allocation_policy::successful_allocations == 1U);
        expect(tracking_allocation_policy::deallocations == 1U);
        expect(tracking_allocation_policy::live_allocations == 0U);
    };

    "the allocator is conforming through standard vector ownership operations"_test = [] {
        using allocator_type =
            fieldpack::detail::aligned_allocator<int, fieldpack::detail::default_alignment, tracking_allocation_policy>;
        using vector_type = std::vector<int, allocator_type>;

        tracking_allocation_policy::reset();
        {
            vector_type original{1, 2, 3};

            vector_type copied{original};
            expect(copied == original);
            copied[0] = 9;
            expect(original[0] == 1);

            vector_type copy_assigned;
            copy_assigned = original;
            expect(copy_assigned == original);
            copy_assigned[1] = 8;
            expect(original[1] == 2);

            auto* copied_data = copied.data();
            vector_type moved{std::move(copied)};
            expect(moved.data() == copied_data);
            expect(moved == vector_type{9, 2, 3});

            vector_type move_assigned;
            auto* copy_assigned_data = copy_assigned.data();
            move_assigned = std::move(copy_assigned);
            expect(move_assigned.data() == copy_assigned_data);
            expect(move_assigned == vector_type{1, 8, 3});

            auto* original_data = original.data();
            auto* moved_data = moved.data();
            original.swap(moved);
            expect(original.data() == moved_data);
            expect(moved.data() == original_data);
            expect(original == vector_type{9, 2, 3});
            expect(moved == vector_type{1, 2, 3});
        }

        expect(tracking_allocation_policy::successful_allocations == tracking_allocation_policy::deallocations);
        expect(tracking_allocation_policy::live_allocations == 0U);
    };

    "field storage is exact, contiguous, aligned, and value initialized"_test = [] {
        for_each_supported_arithmetic_type([]<class T> { check_value_initialized_field_storage<T>(); });
    };

    "empty field storage has an empty contiguous range"_test = [] {
        fieldpack::detail::field_storage<double> storage;

        expect(storage.empty());
        expect(storage.size() == 0U);
        expect(storage.begin() == storage.end());
        expect(std::ranges::distance(storage) == 0);
    };
}
