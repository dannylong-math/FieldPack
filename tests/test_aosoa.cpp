#include "support/table_contract.hpp"
#include "support/tracking_allocation_policy.hpp"

#include <array>
#include <boost/ut.hpp>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <fieldpack/detail/aligned_allocator.hpp>
#include <fieldpack/detail/aosoa_storage.hpp>
#include <fieldpack/layout.hpp>
#include <fieldpack/schema.hpp>
#include <fieldpack/table.hpp>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>

// These tests intentionally use table::operator[] and detail-level unchecked
// backend access after establishing valid logical indices by construction.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

namespace {

using fieldpack_test::tracking_allocation_policy;

template<std::size_t TileExtent>
concept defined_aosoa_layout = requires { typename fieldpack::aosoa<TileExtent>; };

static_assert(defined_aosoa_layout<1>);
static_assert(defined_aosoa_layout<8>);
static_assert(defined_aosoa_layout<64>);
static_assert(!defined_aosoa_layout<0>);

static_assert(fieldpack::valid_layout<fieldpack::aosoa<1>>);
static_assert(fieldpack::valid_layout<fieldpack::aosoa<8>>);
static_assert(fieldpack::valid_layout<fieldpack::aosoa<64>>);
static_assert(!fieldpack::valid_layout<const fieldpack::aosoa<8>>);
static_assert(fieldpack::layout_traits<fieldpack::aosoa<1>>::is_tiled);
static_assert(fieldpack::layout_traits<fieldpack::aosoa<8>>::is_tiled);
static_assert(fieldpack::layout_traits<fieldpack::aosoa<64>>::is_tiled);
static_assert(fieldpack::layout_traits<fieldpack::aosoa<1>>::tile_extent == 1U);
static_assert(fieldpack::layout_traits<fieldpack::aosoa<8>>::tile_extent == 8U);
static_assert(fieldpack::layout_traits<fieldpack::aosoa<64>>::tile_extent == 64U);

template<std::size_t TileExtent>
using mixed_table = fieldpack::table<fieldpack_test::mixed_schema, fieldpack::aosoa<TileExtent>>;

template<std::size_t TileExtent>
using reordered_table = fieldpack::table<fieldpack_test::reordered_schema, fieldpack::aosoa<TileExtent>>;

using qualified_schema_table = fieldpack::table<const fieldpack_test::mixed_schema, fieldpack::aosoa<8>>;

static_assert(std::same_as<qualified_schema_table::schema_type, fieldpack_test::mixed_schema>);
static_assert(std::same_as<qualified_schema_table::layout_type, fieldpack::aosoa<8>>);

template<std::size_t TileExtent>
using tracked_storage =
    fieldpack::detail::aosoa_storage<fieldpack_test::mixed_schema, TileExtent, tracking_allocation_policy>;

template<std::size_t TileExtent> constexpr auto expected_tile_count(std::size_t logical_size) noexcept -> std::size_t
{
    return (logical_size / TileExtent) + static_cast<std::size_t>(logical_size % TileExtent != 0U);
}

template<std::size_t TileExtent> constexpr auto boundary_sizes()
{
    return std::array{
        std::size_t{0},  std::size_t{1},         TileExtent - 1U, TileExtent,
        TileExtent + 1U, (2U * TileExtent) - 1U, 2U * TileExtent, (2U * TileExtent) + 1U,
    };
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

template<std::size_t TileExtent> void check_boundary_sizes_and_tile_counts()
{
    using storage_type = tracked_storage<TileExtent>;

    for (const auto logical_size : boundary_sizes<TileExtent>()) {
        tracking_allocation_policy::reset();
        {
            storage_type values(logical_size);
            boost::ut::expect(values.size() == logical_size);
            boost::ut::expect(values.empty() == (logical_size == 0U));
            boost::ut::expect(values.physical_tile_count() == expected_tile_count<TileExtent>(logical_size));
            boost::ut::expect(tracking_allocation_policy::successful_allocations ==
                              static_cast<std::size_t>(logical_size != 0U));

            for (std::size_t index = 0; index < values.size(); ++index) {
                write_storage_record(values, index, index + 10U);
            }
            for (std::size_t index = 0; index < values.size(); ++index) {
                expect_storage_record(values, index, index + 10U);
            }
        }

        boost::ut::expect(tracking_allocation_policy::successful_allocations ==
                          tracking_allocation_policy::deallocations);
        boost::ut::expect(tracking_allocation_policy::live_allocations == 0U);
    }
}

template<class Tag, std::size_t TileExtent, class Storage> void check_field_mapping(const Storage& values)
{
    using value_type = fieldpack::field_type_t<typename Storage::schema_type, Tag>;

    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto tile_first_index = (index / TileExtent) * TileExtent;
        const auto expected_lane = index % TileExtent;

        const auto* tile_field_start = std::addressof(values.template element<Tag>(tile_first_index));
        const auto* actual = std::addressof(values.template element<Tag>(index));
        // The test compares addresses belonging to the same fixed-size field
        // array without dereferencing any padding lane.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        const auto tile_field_address = reinterpret_cast<std::uintptr_t>(tile_field_start);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        const auto actual_address = reinterpret_cast<std::uintptr_t>(actual);

        boost::ut::expect(tile_field_address % fieldpack::detail::default_alignment == 0U);
        boost::ut::expect(actual_address == tile_field_address + (expected_lane * sizeof(value_type)));
    }
}

struct byte_range {
    std::uintptr_t begin;
    std::uintptr_t end;
};

template<class Tag, std::size_t TileExtent, class Storage>
auto tile_field_range(const Storage& values, std::size_t tile_first_index) -> byte_range
{
    using value_type = fieldpack::field_type_t<typename Storage::schema_type, Tag>;
    const auto* field_start = std::addressof(values.template element<Tag>(tile_first_index));
    // Forming the numeric end address does not access the unobservable padding
    // values; it only verifies that physical field arrays cannot overlap.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto begin = reinterpret_cast<std::uintptr_t>(field_start);
    return {begin, begin + (TileExtent * sizeof(value_type))};
}

void expect_disjoint(byte_range left, byte_range right)
{
    boost::ut::expect(left.end <= right.begin || right.end <= left.begin);
}

template<std::size_t TileExtent> void check_physical_mapping_alignment_and_aliasing()
{
    using storage_type = tracked_storage<TileExtent>;

    tracking_allocation_policy::reset();
    {
        constexpr auto logical_size = (2U * TileExtent) + 1U;
        storage_type values(logical_size);
        boost::ut::expect(values.physical_tile_count() == 3U);

        check_field_mapping<fieldpack_test::x, TileExtent>(values);
        check_field_mapping<fieldpack_test::y, TileExtent>(values);
        check_field_mapping<fieldpack_test::id, TileExtent>(values);
        check_field_mapping<fieldpack_test::count, TileExtent>(values);

        for (std::size_t tile = 0; tile < values.physical_tile_count(); ++tile) {
            const auto tile_first_index = tile * TileExtent;
            const auto x_range = tile_field_range<fieldpack_test::x, TileExtent>(values, tile_first_index);
            const auto y_range = tile_field_range<fieldpack_test::y, TileExtent>(values, tile_first_index);
            const auto id_range = tile_field_range<fieldpack_test::id, TileExtent>(values, tile_first_index);
            const auto count_range = tile_field_range<fieldpack_test::count, TileExtent>(values, tile_first_index);

            expect_disjoint(x_range, y_range);
            expect_disjoint(x_range, id_range);
            expect_disjoint(x_range, count_range);
            expect_disjoint(y_range, id_range);
            expect_disjoint(y_range, count_range);
            expect_disjoint(id_range, count_range);
        }
    }

    boost::ut::expect(tracking_allocation_policy::successful_allocations == 1U);
    boost::ut::expect(tracking_allocation_policy::deallocations == 1U);
    boost::ut::expect(tracking_allocation_policy::live_allocations == 0U);
}

template<std::size_t TileExtent> void check_padding_and_resize_contract()
{
    using table_type = mixed_table<TileExtent>;

    if constexpr (TileExtent > 1U) {
        table_type grow_into_padding(1);
        fieldpack_test::write_record(grow_into_padding, {0, 20U});
        grow_into_padding.resize(TileExtent);
        fieldpack_test::expect_record(grow_into_padding, {0, 20U});
        for (std::size_t index = 1; index < grow_into_padding.size(); ++index) {
            const auto row = static_cast<const table_type&>(grow_into_padding)[index];
            boost::ut::expect(row.template get<fieldpack_test::x>() == 0.0F);
            boost::ut::expect(row.template get<fieldpack_test::y>() == 0.0);
            boost::ut::expect(row.template get<fieldpack_test::id>() == std::uint32_t{});
            boost::ut::expect(row.template get<fieldpack_test::count>() == std::int64_t{});
        }

        fieldpack_test::write_record(grow_into_padding, {TileExtent - 1U, 30U});
        grow_into_padding.resize(TileExtent - 1U);
        grow_into_padding.resize(TileExtent);
        const auto restored_lane = static_cast<const table_type&>(grow_into_padding)[TileExtent - 1U];
        boost::ut::expect(restored_lane.template get<fieldpack_test::x>() == 0.0F);
        boost::ut::expect(restored_lane.template get<fieldpack_test::y>() == 0.0);
        boost::ut::expect(restored_lane.template get<fieldpack_test::id>() == std::uint32_t{});
        boost::ut::expect(restored_lane.template get<fieldpack_test::count>() == std::int64_t{});
    }

    constexpr auto original_size = (2U * TileExtent) + 1U;
    table_type across_boundaries(original_size);
    for (std::size_t index = 0; index < across_boundaries.size(); ++index) {
        fieldpack_test::write_record(across_boundaries, {index, index + 40U});
    }

    across_boundaries.resize(TileExtent);
    across_boundaries.resize((3U * TileExtent) + 1U);
    for (std::size_t index = 0; index < TileExtent; ++index) {
        fieldpack_test::expect_record(across_boundaries, {index, index + 40U});
    }
    for (std::size_t index = TileExtent; index < across_boundaries.size(); ++index) {
        const auto row = static_cast<const table_type&>(across_boundaries)[index];
        boost::ut::expect(row.template get<fieldpack_test::x>() == 0.0F);
        boost::ut::expect(row.template get<fieldpack_test::y>() == 0.0);
        boost::ut::expect(row.template get<fieldpack_test::id>() == std::uint32_t{});
        boost::ut::expect(row.template get<fieldpack_test::count>() == std::int64_t{});
    }

    const auto logical_end = across_boundaries.size();
    boost::ut::expect(
        boost::ut::throws<std::out_of_range>([&] { static_cast<void>(across_boundaries.at(logical_end)); }));
    const auto& observed = across_boundaries;
    boost::ut::expect(boost::ut::throws<std::out_of_range>([&] { static_cast<void>(observed.at(logical_end)); }));
}

template<std::size_t TileExtent> void check_allocation_failures_and_overflow()
{
    using storage_type = tracked_storage<TileExtent>;

    tracking_allocation_policy::reset();
    tracking_allocation_policy::fail_on_attempt = 1U;
    boost::ut::expect(boost::ut::throws<std::bad_alloc>([] { static_cast<void>(storage_type{TileExtent + 1U}); }));
    boost::ut::expect(tracking_allocation_policy::successful_allocations == 0U);
    boost::ut::expect(tracking_allocation_policy::live_allocations == 0U);

    tracking_allocation_policy::reset();
    {
        constexpr auto original_size = TileExtent + 1U;
        storage_type values(original_size);
        for (std::size_t index = 0; index < values.size(); ++index) {
            write_storage_record(values, index, index + 60U);
        }

        tracking_allocation_policy::fail_on_attempt = tracking_allocation_policy::allocation_attempts + 1U;
        boost::ut::expect(boost::ut::throws<std::bad_alloc>([&] { values.resize((4U * TileExtent) + 1U); }));
        boost::ut::expect(values.size() == original_size);
        boost::ut::expect(values.physical_tile_count() == 2U);
        boost::ut::expect(tracking_allocation_policy::live_allocations == 1U);
        for (std::size_t index = 0; index < values.size(); ++index) {
            expect_storage_record(values, index, index + 60U);
        }
    }
    boost::ut::expect(tracking_allocation_policy::live_allocations == 0U);

    tracking_allocation_policy::reset();
    boost::ut::expect(boost::ut::throws<std::bad_array_new_length>(
        [] { static_cast<void>(storage_type{std::numeric_limits<std::size_t>::max()}); }));
    boost::ut::expect(tracking_allocation_policy::allocation_attempts == 0U);
    boost::ut::expect(tracking_allocation_policy::live_allocations == 0U);
}

} // namespace

int main() // NOLINT(bugprone-exception-escape) -- Boost.UT owns top-level test exception handling
{
    using namespace boost::ut;

    "AoSoA tables satisfy the scalar contract for every representative extent"_test = [] {
        fieldpack_test::check_scalar_table_contract<mixed_table<1>>();
        fieldpack_test::check_scalar_table_contract<mixed_table<8>>();
        fieldpack_test::check_scalar_table_contract<mixed_table<64>>();
    };

    "AoSoA tables resolve tags independently of schema field order"_test = [] {
        fieldpack_test::check_scalar_table_contract<reordered_table<8>>();
    };

    "AoSoA tables normalize top-level schema cv-qualification"_test = [] {
        qualified_schema_table values(1);
        values[0].template get<fieldpack_test::x>() = 3.5F;

        const auto& observed = values;
        expect(observed[0].template get<fieldpack_test::x>() == 3.5F);
    };

    "AoSoA logical boundaries allocate the expected complete tiles"_test = [] {
        check_boundary_sizes_and_tile_counts<1>();
        check_boundary_sizes_and_tile_counts<8>();
        check_boundary_sizes_and_tile_counts<64>();
    };

    "AoSoA index mapping is contiguous aligned and non-aliasing within each tile"_test = [] {
        check_physical_mapping_alignment_and_aliasing<1>();
        check_physical_mapping_alignment_and_aliasing<8>();
        check_physical_mapping_alignment_and_aliasing<64>();
    };

    "AoSoA resize operations hide padding clear stale lanes and preserve the live prefix"_test = [] {
        check_padding_and_resize_contract<1>();
        check_padding_and_resize_contract<8>();
        check_padding_and_resize_contract<64>();
    };

    "AoSoA allocation failures preserve state and arithmetic overflow precedes allocation"_test = [] {
        check_allocation_failures_and_overflow<1>();
        check_allocation_failures_and_overflow<8>();
        check_allocation_failures_and_overflow<64>();
    };
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
