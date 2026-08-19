#include "support/table_contract.hpp"
#include "support/tracking_allocation_policy.hpp"

#include <boost/ut.hpp>
#include <cstddef>
#include <cstdint>
#include <fieldpack/detail/aligned_allocator.hpp>
#include <fieldpack/detail/soa_storage.hpp>
#include <fieldpack/layout.hpp>
#include <fieldpack/schema.hpp>
#include <fieldpack/table.hpp>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>

namespace {

using fieldpack_test::tracking_allocation_policy;

using mixed_table = fieldpack::table<fieldpack_test::mixed_schema, fieldpack::soa>;

static_assert(fieldpack::valid_layout<fieldpack::soa>);
static_assert(!fieldpack::valid_layout<const fieldpack::soa>);
static_assert(!fieldpack::valid_layout<int>);
static_assert(!fieldpack::layout_traits<fieldpack::soa>::is_tiled);

template<class Tag, class Table> void check_contiguous_aligned_column(Table& values)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    using value_type = std::remove_reference_t<decltype(values[0].template get<Tag>())>;

    // The table is known to be non-empty and this test intentionally exercises
    // the unchecked accessor and compares its raw column addresses.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    auto* first_value = std::addressof(values[0].template get<Tag>());
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto first_address = reinterpret_cast<std::uintptr_t>(first_value);
    boost::ut::expect(first_address % fieldpack::detail::default_alignment == 0U);

    for (std::size_t index = 0; index < values.size(); ++index) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        const auto actual_address = reinterpret_cast<std::uintptr_t>(std::addressof(values[index].template get<Tag>()));
        boost::ut::expect(actual_address == first_address + (index * sizeof(value_type)));
    }
}

template<class T, class AllocationPolicy = fieldpack::detail::aligned_new_policy> void check_allocator_control_flow()
{
    using allocator_type =
        fieldpack::detail::aligned_allocator<T, fieldpack::detail::default_alignment, AllocationPolicy>;

    allocator_type allocator;
    auto* empty_allocation = allocator.allocate(0);
    boost::ut::expect(empty_allocation == nullptr);
    allocator.deallocate(empty_allocation, 0);
    allocator.deallocate(nullptr, 0);

    auto* allocation = allocator.allocate(1);
    boost::ut::expect(allocation != nullptr);
    allocator.deallocate(allocation, 1);

    constexpr auto largest_size = std::numeric_limits<std::size_t>::max();
    if constexpr (allocator_type::max_size() < largest_size) {
        constexpr auto overflowing_count = allocator_type::max_size() + 1U;
        boost::ut::expect(boost::ut::throws<std::bad_array_new_length>(
            [&] { static_cast<void>(allocator.allocate(overflowing_count)); }));
    }
}

template<class Storage> void write_storage_record(Storage& values, std::size_t index, std::size_t seed)
{
    values.template element<fieldpack_test::x>(index) = static_cast<float>(seed) + 0.25F;
    values.template element<fieldpack_test::y>(index) = static_cast<double>(seed) + 0.5;
    values.template element<fieldpack_test::id>(index) = static_cast<std::uint32_t>(1'000U + seed);
    values.template element<fieldpack_test::count>(index) = -static_cast<std::int64_t>(seed) - 7;
}

template<class Storage> void expect_storage_record(const Storage& values, std::size_t index, std::size_t seed)
{
    boost::ut::expect(values.template element<fieldpack_test::x>(index) == static_cast<float>(seed) + 0.25F);
    boost::ut::expect(values.template element<fieldpack_test::y>(index) == static_cast<double>(seed) + 0.5);
    boost::ut::expect(values.template element<fieldpack_test::id>(index) == static_cast<std::uint32_t>(1'000U + seed));
    boost::ut::expect(values.template element<fieldpack_test::count>(index) == -static_cast<std::int64_t>(seed) - 7);
}

} // namespace

int main() // NOLINT(bugprone-exception-escape) -- Boost.UT owns top-level test exception handling
{
    using namespace boost::ut;

    "every non-empty SoA column is contiguous and 64-byte aligned"_test = [] {
        mixed_table values(257);

        check_contiguous_aligned_column<fieldpack_test::x>(values);
        check_contiguous_aligned_column<fieldpack_test::y>(values);
        check_contiguous_aligned_column<fieldpack_test::id>(values);
        check_contiguous_aligned_column<fieldpack_test::count>(values);
    };

    "allocator control flow is covered for every SoA field and policy"_test = [] {
        const auto check_field_type = []<class T> {
            check_allocator_control_flow<T>();

            tracking_allocation_policy::reset();
            check_allocator_control_flow<T, tracking_allocation_policy>();
            expect(tracking_allocation_policy::successful_allocations == 1U);
            expect(tracking_allocation_policy::deallocations == 1U);
            expect(tracking_allocation_policy::live_allocations == 0U);
        };

        check_field_type.template operator()<float>();
        check_field_type.template operator()<double>();
        check_field_type.template operator()<std::uint32_t>();
        check_field_type.template operator()<std::int64_t>();
    };

    "partial SoA construction is cleaned up after failure in every column"_test = [] {
        using storage_type = fieldpack::detail::soa_storage<fieldpack_test::mixed_schema, tracking_allocation_policy>;

        constexpr auto field_count = fieldpack::field_count_v<fieldpack_test::mixed_schema>;
        for (std::size_t failing_column = 1; failing_column <= field_count; ++failing_column) {
            tracking_allocation_policy::reset();
            tracking_allocation_policy::fail_on_attempt = failing_column;

            expect(throws<std::bad_alloc>([] { static_cast<void>(storage_type{5}); }));
            expect(tracking_allocation_policy::successful_allocations == failing_column - 1U);
            expect(tracking_allocation_policy::deallocations == failing_column - 1U);
            expect(tracking_allocation_policy::live_allocations == 0U);
        }
    };

    "partial SoA copying is cleaned up after failure in every column"_test = [] {
        using storage_type = fieldpack::detail::soa_storage<fieldpack_test::mixed_schema, tracking_allocation_policy>;

        tracking_allocation_policy::reset();
        {
            storage_type source(5);
            for (std::size_t index = 0; index < source.size(); ++index) {
                write_storage_record(source, index, index + 40U);
            }

            const auto source_live_allocations = tracking_allocation_policy::live_allocations;
            constexpr auto field_count = fieldpack::field_count_v<fieldpack_test::mixed_schema>;
            for (std::size_t failing_column = 1; failing_column <= field_count; ++failing_column) {
                const auto successful_before = tracking_allocation_policy::successful_allocations;
                const auto deallocations_before = tracking_allocation_policy::deallocations;
                tracking_allocation_policy::fail_on_attempt =
                    tracking_allocation_policy::allocation_attempts + failing_column;

                expect(throws<std::bad_alloc>([&] { static_cast<void>(storage_type{source}); }));
                expect(tracking_allocation_policy::successful_allocations - successful_before == failing_column - 1U);
                expect(tracking_allocation_policy::deallocations - deallocations_before == failing_column - 1U);
                expect(tracking_allocation_policy::live_allocations == source_live_allocations);
            }

            tracking_allocation_policy::fail_on_attempt = 0;
            storage_type copied(source);
            for (std::size_t index = 0; index < copied.size(); ++index) {
                expect_storage_record(copied, index, index + 40U);
            }
            copied.template element<fieldpack_test::x>(0) = -1.0F;
            expect(source.template element<fieldpack_test::x>(0) == 40.25F);
        }

        expect(tracking_allocation_policy::successful_allocations == tracking_allocation_policy::deallocations);
        expect(tracking_allocation_policy::live_allocations == 0U);
    };

    "an allocation failure preserves the original SoA backend"_test = [] {
        using storage_type = fieldpack::detail::soa_storage<fieldpack_test::mixed_schema, tracking_allocation_policy>;

        tracking_allocation_policy::reset();
        {
            storage_type values(5);
            for (std::size_t index = 0; index < values.size(); ++index) {
                write_storage_record(values, index, index + 50U);
            }

            auto* original_x = std::addressof(values.template element<fieldpack_test::x>(0));
            auto* original_y = std::addressof(values.template element<fieldpack_test::y>(0));
            auto* original_id = std::addressof(values.template element<fieldpack_test::id>(0));
            auto* original_count = std::addressof(values.template element<fieldpack_test::count>(0));
            const auto original_live_allocations = tracking_allocation_policy::live_allocations;

            // Fail after replacement storage has successfully allocated two
            // columns, exercising cleanup of a partially constructed backend.
            tracking_allocation_policy::fail_on_attempt = tracking_allocation_policy::allocation_attempts + 3U;
            expect(throws<std::bad_alloc>([&] { values.resize(12); }));

            expect(values.size() == 5U);
            expect(tracking_allocation_policy::live_allocations == original_live_allocations);
            expect(std::addressof(values.template element<fieldpack_test::x>(0)) == original_x);
            expect(std::addressof(values.template element<fieldpack_test::y>(0)) == original_y);
            expect(std::addressof(values.template element<fieldpack_test::id>(0)) == original_id);
            expect(std::addressof(values.template element<fieldpack_test::count>(0)) == original_count);

            for (std::size_t index = 0; index < values.size(); ++index) {
                expect_storage_record(values, index, index + 50U);
            }

            tracking_allocation_policy::fail_on_attempt = 0;
            const auto attempts_before_same_size = tracking_allocation_policy::allocation_attempts;
            values.resize(values.size());
            expect(tracking_allocation_policy::allocation_attempts == attempts_before_same_size);

            values.resize(3);
            expect(values.size() == 3U);
            for (std::size_t index = 0; index < values.size(); ++index) {
                expect_storage_record(values, index, index + 50U);
            }

            values.resize(7);
            expect(values.size() == 7U);
            for (std::size_t index = 0; index < 3; ++index) {
                expect_storage_record(values, index, index + 50U);
            }
            for (std::size_t index = 3; index < values.size(); ++index) {
                expect(values.template element<fieldpack_test::x>(index) == 0.0F);
                expect(values.template element<fieldpack_test::y>(index) == 0.0);
                expect(values.template element<fieldpack_test::id>(index) == std::uint32_t{});
                expect(values.template element<fieldpack_test::count>(index) == std::int64_t{});
            }
        }

        expect(tracking_allocation_policy::successful_allocations == tracking_allocation_policy::deallocations);
        expect(tracking_allocation_policy::live_allocations == 0U);
    };
}
