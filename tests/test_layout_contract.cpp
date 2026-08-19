#include "support/table_contract.hpp"

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
#include <new>
#include <stdexcept>
#include <utility>

// The shared contract intentionally exercises unchecked scalar access only
// after constructing valid logical indices in its generic workload.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

namespace {

template<class Schema, class Layout> using table_for = fieldpack::table<Schema, Layout>;

using mixed_soa = table_for<fieldpack_test::mixed_schema, fieldpack::soa>;
using mixed_aosoa_1 = table_for<fieldpack_test::mixed_schema, fieldpack::aosoa<1>>;
using mixed_aosoa_8 = table_for<fieldpack_test::mixed_schema, fieldpack::aosoa<8>>;
using mixed_aosoa_64 = table_for<fieldpack_test::mixed_schema, fieldpack::aosoa<64>>;

using reordered_soa = table_for<fieldpack_test::reordered_schema, fieldpack::soa>;
using reordered_aosoa_1 = table_for<fieldpack_test::reordered_schema, fieldpack::aosoa<1>>;
using reordered_aosoa_8 = table_for<fieldpack_test::reordered_schema, fieldpack::aosoa<8>>;
using reordered_aosoa_64 = table_for<fieldpack_test::reordered_schema, fieldpack::aosoa<64>>;

template<class Table, class Schema>
concept layout_neutral_scalar_table =
    std::same_as<typename Table::schema_type, Schema> && std::same_as<typename Table::size_type, std::size_t> &&
    std::default_initializable<Table> && std::copyable<Table> && std::movable<Table> &&
    requires(Table& mutable_values, const Table& immutable_values, std::size_t index) {
        { mutable_values.size() } noexcept -> std::same_as<std::size_t>;
        { immutable_values.size() } noexcept -> std::same_as<std::size_t>;
        { mutable_values.empty() } noexcept -> std::same_as<bool>;
        { immutable_values.empty() } noexcept -> std::same_as<bool>;
        { mutable_values[index].template get<fieldpack_test::x>() } noexcept -> std::same_as<float&>;
        { immutable_values[index].template get<fieldpack_test::x>() } noexcept -> std::same_as<const float&>;
        { mutable_values.at(index).template get<fieldpack_test::id>() } -> std::same_as<std::uint32_t&>;
        { immutable_values.at(index).template get<fieldpack_test::id>() } -> std::same_as<const std::uint32_t&>;
        { mutable_values.resize(index) } -> std::same_as<void>;
    };

static_assert(layout_neutral_scalar_table<mixed_soa, fieldpack_test::mixed_schema>);
static_assert(layout_neutral_scalar_table<mixed_aosoa_1, fieldpack_test::mixed_schema>);
static_assert(layout_neutral_scalar_table<mixed_aosoa_8, fieldpack_test::mixed_schema>);
static_assert(layout_neutral_scalar_table<mixed_aosoa_64, fieldpack_test::mixed_schema>);
static_assert(layout_neutral_scalar_table<reordered_soa, fieldpack_test::reordered_schema>);
static_assert(layout_neutral_scalar_table<reordered_aosoa_1, fieldpack_test::reordered_schema>);
static_assert(layout_neutral_scalar_table<reordered_aosoa_8, fieldpack_test::reordered_schema>);
static_assert(layout_neutral_scalar_table<reordered_aosoa_64, fieldpack_test::reordered_schema>);

template<class... Tables> void check_identical_scalar_contracts()
{
    (fieldpack_test::check_scalar_table_contract<Tables>(), ...);
}

struct record_snapshot {
    float x;
    double y;
    std::uint32_t id;
    std::int64_t count;

    auto operator==(const record_snapshot&) const -> bool = default;
};

template<class Row> auto snapshot(const Row& row) -> record_snapshot
{
    return {
        .x = row.template get<fieldpack_test::x>(),
        .y = row.template get<fieldpack_test::y>(),
        .id = row.template get<fieldpack_test::id>(),
        .count = row.template get<fieldpack_test::count>(),
    };
}

template<class Table> auto run_layout_neutral_workload() -> std::array<record_snapshot, 6>
{
    Table values(5);
    for (std::size_t index = 0; index < values.size(); ++index) {
        fieldpack_test::write_record(values, {index, index + 100U});
    }

    values.resize(9);
    for (std::size_t index = 5; index < values.size(); ++index) {
        fieldpack_test::write_record(values, {index, index + 200U});
    }

    values.resize(4);
    values.resize(6);

    Table copied(values);
    Table assigned;
    assigned = copied;
    Table moved(std::move(assigned));

    std::array<record_snapshot, 6> result{};
    const auto& observed = moved;
    for (std::size_t index = 0; index < result.size(); ++index) {
        // The loop condition establishes the fixed-array bound.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        result[index] = snapshot(observed.at(index));
    }
    return result;
}

template<class... Tables> void expect_identical_workload_results()
{
    const auto expected = run_layout_neutral_workload<mixed_soa>();
    (boost::ut::expect(run_layout_neutral_workload<Tables>() == expected), ...);

    const std::array expected_records{
        record_snapshot{.x = 100.25F, .y = 100.5, .id = 1'100U, .count = -107},
        record_snapshot{.x = 101.25F, .y = 101.5, .id = 1'101U, .count = -108},
        record_snapshot{.x = 102.25F, .y = 102.5, .id = 1'102U, .count = -109},
        record_snapshot{.x = 103.25F, .y = 103.5, .id = 1'103U, .count = -110},
        record_snapshot{},
        record_snapshot{},
    };
    boost::ut::expect(expected == expected_records);
}

template<class T> void check_default_allocator_control_flow()
{
    using allocator_type = fieldpack::detail::aligned_allocator<T>;

    allocator_type allocator;
    auto* empty_allocation = allocator.allocate(0U);
    boost::ut::expect(empty_allocation == nullptr);
    allocator.deallocate(empty_allocation, 0U);
    allocator.deallocate(nullptr, 0U);

    auto* allocation = allocator.allocate(1U);
    boost::ut::expect(allocation != nullptr);
    allocator.deallocate(allocation, 1U);

    constexpr auto overflowing_count = allocator_type::max_size() + 1U;
    boost::ut::expect(boost::ut::throws<std::bad_array_new_length>(
        [&] { static_cast<void>(allocator.allocate(overflowing_count)); }));
}

template<class Schema, std::size_t TileExtent> struct schema_tile;

template<class... Fields, std::size_t TileExtent> struct schema_tile<fieldpack::schema<Fields...>, TileExtent> {
    using type = fieldpack::detail::tile_storage<TileExtent, Fields...>;
};

template<class Schema, std::size_t TileExtent> using schema_tile_t = schema_tile<Schema, TileExtent>::type;

template<class Schema, std::size_t TileExtent> void check_production_aosoa_control_flow()
{
    using storage_type = fieldpack::detail::aosoa_storage<Schema, TileExtent>;

    storage_type values((2U * TileExtent) + 1U);
    values.resize(values.size());
    values.resize(2U * TileExtent);
    values.resize(TileExtent + 1U);
    values.resize(TileExtent + 2U);
    values.resize(2U * TileExtent);
    values.resize((2U * TileExtent) + 1U);
    values.resize(0U);
    values.resize(TileExtent);

    boost::ut::expect(values.size() == TileExtent);
    boost::ut::expect(boost::ut::throws<std::bad_array_new_length>(
        [] { static_cast<void>(storage_type{std::numeric_limits<std::size_t>::max()}); }));
}

template<class Schema, std::size_t... TileExtents>
void check_generated_aosoa_control_flow(std::index_sequence<TileExtents...> /*unused*/)
{
    (check_default_allocator_control_flow<schema_tile_t<Schema, TileExtents>>(), ...);
    (check_production_aosoa_control_flow<Schema, TileExtents>(), ...);
}

template<class Table> void check_cv_normalization()
{
    static_assert(std::same_as<typename Table::schema_type, fieldpack_test::mixed_schema>);

    Table values(1);
    values.at(0).template get<fieldpack_test::x>() = 3.5F;
    boost::ut::expect(boost::ut::throws<std::out_of_range>([&] { static_cast<void>(values.at(values.size())); }));

    const auto& observed = values;
    boost::ut::expect(observed.at(0).template get<fieldpack_test::x>() == 3.5F);
    boost::ut::expect(boost::ut::throws<std::out_of_range>([&] { static_cast<void>(observed.at(observed.size())); }));
}

} // namespace

int main() // NOLINT(bugprone-exception-escape) -- Boost.UT owns top-level test exception handling
{
    using namespace boost::ut;

    "the shared scalar contract is identical for mixed-schema SoA and every AoSoA extent"_test = [] {
        check_identical_scalar_contracts<mixed_soa, mixed_aosoa_1, mixed_aosoa_8, mixed_aosoa_64>();
    };

    "the shared scalar contract is independent of layout and schema field order"_test = [] {
        check_identical_scalar_contracts<reordered_soa, reordered_aosoa_1, reordered_aosoa_8, reordered_aosoa_64>();
    };

    "one branch-free scalar workload produces identical records for every layout"_test = [] {
        expect_identical_workload_results<mixed_aosoa_1, mixed_aosoa_8, mixed_aosoa_64, reordered_soa,
                                          reordered_aosoa_1, reordered_aosoa_8, reordered_aosoa_64>();
    };

    "top-level schema cv-qualification is normalized for every layout"_test = [] {
        check_cv_normalization<table_for<const fieldpack_test::mixed_schema, fieldpack::soa>>();
        check_cv_normalization<table_for<const fieldpack_test::mixed_schema, fieldpack::aosoa<1>>>();
        check_cv_normalization<table_for<const fieldpack_test::mixed_schema, fieldpack::aosoa<8>>>();
        check_cv_normalization<table_for<const fieldpack_test::mixed_schema, fieldpack::aosoa<64>>>();
    };

    "generated production backends cover allocator and resize control flow in this executable"_test = [] {
        check_default_allocator_control_flow<float>();
        check_default_allocator_control_flow<double>();
        check_default_allocator_control_flow<std::uint32_t>();
        check_default_allocator_control_flow<std::int64_t>();

        check_generated_aosoa_control_flow<fieldpack_test::mixed_schema>(std::index_sequence<1, 8, 64>{});
        check_generated_aosoa_control_flow<fieldpack_test::reordered_schema>(std::index_sequence<1, 8, 64>{});
    };
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
