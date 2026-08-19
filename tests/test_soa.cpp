#include "support/table_contract.hpp"

#include <boost/ut.hpp>
#include <cstddef>
#include <cstdint>
#include <fieldpack/detail/aligned_allocator.hpp>
#include <fieldpack/detail/soa_storage.hpp>
#include <fieldpack/layout.hpp>
#include <fieldpack/table.hpp>
#include <memory>
#include <new>
#include <type_traits>

namespace {

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

    static void deallocate_bytes(void* allocation, std::size_t /*unused*/, std::size_t alignment) noexcept
    {
        if (allocation == nullptr) {
            return;
        }

        ::operator delete(allocation, std::align_val_t{alignment});
        ++deallocations;
        --live_allocations;
    }
};

using mixed_table = fieldpack::table<fieldpack_test::mixed_schema, fieldpack::soa>;
using reordered_table = fieldpack::table<fieldpack_test::reordered_schema, fieldpack::soa>;

template<class Tag, class Table> void check_contiguous_aligned_column(Table& values)
{
    using value_type = std::remove_reference_t<decltype(values[0].template get<Tag>())>;

    auto* first_value = std::addressof(values[0].template get<Tag>());
    const auto first_address = reinterpret_cast<std::uintptr_t>(first_value);
    boost::ut::expect(first_address % fieldpack::detail::default_alignment == 0U);

    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto actual_address = reinterpret_cast<std::uintptr_t>(std::addressof(values[index].template get<Tag>()));
        boost::ut::expect(actual_address == first_address + index * sizeof(value_type));
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

int main()
{
    using namespace boost::ut;

    "SoA tables satisfy the scalar contract for a mixed schema"_test = [] {
        fieldpack_test::check_scalar_table_contract<mixed_table>();
    };

    "SoA tables resolve tags independently of schema field order"_test = [] {
        fieldpack_test::check_scalar_table_contract<reordered_table>();
    };

    "every non-empty SoA column is contiguous and 64-byte aligned"_test = [] {
        mixed_table values(257);

        check_contiguous_aligned_column<fieldpack_test::x>(values);
        check_contiguous_aligned_column<fieldpack_test::y>(values);
        check_contiguous_aligned_column<fieldpack_test::id>(values);
        check_contiguous_aligned_column<fieldpack_test::count>(values);
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
        }

        expect(tracking_allocation_policy::successful_allocations == tracking_allocation_policy::deallocations);
        expect(tracking_allocation_policy::live_allocations == 0U);
    };
}
