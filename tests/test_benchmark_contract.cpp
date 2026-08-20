#include "benchmark_support.hpp"

#include <algorithm>
#include <array>
#include <boost/ut.hpp>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <fieldpack/detail/aligned_allocator.hpp>
#include <fieldpack/detail/aosoa_storage.hpp>
#include <fieldpack/layout.hpp>
#include <fieldpack/schema.hpp>
#include <fieldpack/table.hpp>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <utility>

// Every unchecked table access in this suite is bounded by the table or raw
// array size constructed in the same test helper.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

namespace {

constexpr double time_step = 0.125;
constexpr double subset_scale = -0.375;

void expect_close(double actual, double expected)
{
    if (actual == expected) {
        boost::ut::expect(true);
        return;
    }
    const auto scale = std::max(std::abs(actual), std::abs(expected));
    constexpr auto relative_tolerance = 64.0 * std::numeric_limits<double>::epsilon();
    constexpr auto absolute_tolerance = 1.0e-300;
    boost::ut::expect(std::isfinite(actual));
    boost::ut::expect(std::isfinite(expected));
    boost::ut::expect(std::abs(actual - expected) <= absolute_tolerance + (relative_tolerance * scale));
}

void reference_polynomial(fieldpack_benchmark::polynomial_arrays& values)
{
    for (std::size_t index = 0U; index < values.output.size(); ++index) {
        const auto input = values.input.at(index);
        auto result = values.coefficient_3.at(index);
        result = (result * input) + values.coefficient_2.at(index);
        result = (result * input) + values.coefficient_1.at(index);
        result = (result * input) + values.coefficient_0.at(index);
        values.output.at(index) = result;
    }
}

void reference_drift(fieldpack_benchmark::drift_arrays& values)
{
    for (std::size_t index = 0U; index < values.position_x.size(); ++index) {
        values.position_x.at(index) += time_step * values.velocity_x.at(index);
        values.position_y.at(index) += time_step * values.velocity_y.at(index);
    }
}

void reference_field_subset(fieldpack_benchmark::field_subset_arrays& values)
{
    for (std::size_t index = 0U; index < values.hot_target.size(); ++index) {
        values.hot_target.at(index) += subset_scale * values.hot_source.at(index);
    }
}

[[nodiscard]] auto reference_reduction(const fieldpack_benchmark::reduction_arrays& values) -> double
{
    double checksum = 0.0;
    for (std::size_t index = 0U; index < values.a.size(); ++index) {
        checksum += 1.0;
        checksum += values.a.at(index);
        checksum += values.b.at(index);
        checksum += values.c.at(index);
        checksum += values.d.at(index);
    }
    return checksum;
}

void expect_reduction_close(double actual, double expected, const fieldpack_benchmark::reduction_arrays& values)
{
    // release-max permits reassociation. The fixture deliberately contains
    // cancelling +/-1e100 terms, so the valid forward-error bound must scale
    // with the sum of magnitudes rather than the small cancelled result.
    double magnitude_sum = 0.0;
    for (std::size_t index = 0U; index < values.a.size(); ++index) {
        magnitude_sum += 1.0;
        magnitude_sum += std::abs(values.a.at(index));
        magnitude_sum += std::abs(values.b.at(index));
        magnitude_sum += std::abs(values.c.at(index));
        magnitude_sum += std::abs(values.d.at(index));
    }
    constexpr auto rounding_factor = 64.0 * std::numeric_limits<double>::epsilon();
    constexpr auto absolute_tolerance = 1.0e-12;
    boost::ut::expect(std::isfinite(actual));
    boost::ut::expect(std::isfinite(expected));
    boost::ut::expect(std::abs(actual - expected) <= absolute_tolerance + (rounding_factor * magnitude_sum));
}

void expect_polynomial_raw(const fieldpack_benchmark::polynomial_arrays& actual,
                           const fieldpack_benchmark::polynomial_arrays& expected)
{
    boost::ut::expect(actual.coefficient_0 == expected.coefficient_0);
    boost::ut::expect(actual.coefficient_1 == expected.coefficient_1);
    boost::ut::expect(actual.coefficient_2 == expected.coefficient_2);
    boost::ut::expect(actual.coefficient_3 == expected.coefficient_3);
    boost::ut::expect(actual.input == expected.input);
    boost::ut::expect(actual.output.size() == expected.output.size());
    for (std::size_t index = 0U; index < actual.output.size(); ++index) {
        expect_close(actual.output.at(index), expected.output.at(index));
    }
}

template<class Layout>
void expect_polynomial_table(const fieldpack::table<fieldpack_benchmark::polynomial_schema, Layout>& actual,
                             const fieldpack_benchmark::polynomial_arrays& expected)
{
    boost::ut::expect(actual.size() == expected.output.size());
    for (std::size_t index = 0U; index < actual.size(); ++index) {
        const auto row = actual[index];
        boost::ut::expect(row.template get<fieldpack_benchmark::tags::coefficient_0>() ==
                          expected.coefficient_0.at(index));
        boost::ut::expect(row.template get<fieldpack_benchmark::tags::coefficient_1>() ==
                          expected.coefficient_1.at(index));
        boost::ut::expect(row.template get<fieldpack_benchmark::tags::coefficient_2>() ==
                          expected.coefficient_2.at(index));
        boost::ut::expect(row.template get<fieldpack_benchmark::tags::coefficient_3>() ==
                          expected.coefficient_3.at(index));
        boost::ut::expect(row.template get<fieldpack_benchmark::tags::polynomial_input>() == expected.input.at(index));
        expect_close(row.template get<fieldpack_benchmark::tags::polynomial_output>(), expected.output.at(index));
    }
}

void expect_drift_raw(const fieldpack_benchmark::drift_arrays& actual,
                      const fieldpack_benchmark::drift_arrays& expected)
{
    boost::ut::expect(actual.velocity_x == expected.velocity_x);
    boost::ut::expect(actual.velocity_y == expected.velocity_y);
    for (std::size_t index = 0U; index < actual.position_x.size(); ++index) {
        expect_close(actual.position_x.at(index), expected.position_x.at(index));
        expect_close(actual.position_y.at(index), expected.position_y.at(index));
    }
}

template<class Layout>
void expect_drift_table(const fieldpack::table<fieldpack_benchmark::drift_schema, Layout>& actual,
                        const fieldpack_benchmark::drift_arrays& expected)
{
    boost::ut::expect(actual.size() == expected.position_x.size());
    for (std::size_t index = 0U; index < actual.size(); ++index) {
        const auto row = actual[index];
        expect_close(row.template get<fieldpack_benchmark::tags::position_x>(), expected.position_x.at(index));
        expect_close(row.template get<fieldpack_benchmark::tags::position_y>(), expected.position_y.at(index));
        boost::ut::expect(row.template get<fieldpack_benchmark::tags::velocity_x>() == expected.velocity_x.at(index));
        boost::ut::expect(row.template get<fieldpack_benchmark::tags::velocity_y>() == expected.velocity_y.at(index));
    }
}

void expect_field_subset_raw(const fieldpack_benchmark::field_subset_arrays& actual,
                             const fieldpack_benchmark::field_subset_arrays& expected)
{
    boost::ut::expect(actual.hot_source == expected.hot_source);
    boost::ut::expect(actual.cold == expected.cold);
    for (std::size_t index = 0U; index < actual.hot_target.size(); ++index) {
        expect_close(actual.hot_target.at(index), expected.hot_target.at(index));
    }
}

template<class Layout>
void expect_field_subset_table(const fieldpack::table<fieldpack_benchmark::field_subset_schema, Layout>& actual,
                               const fieldpack_benchmark::field_subset_arrays& expected)
{
    boost::ut::expect(actual.size() == expected.hot_target.size());
    for (std::size_t index = 0U; index < actual.size(); ++index) {
        const auto row = actual[index];
        boost::ut::expect(row.template get<fieldpack_benchmark::tags::hot_source>() == expected.hot_source.at(index));
        expect_close(row.template get<fieldpack_benchmark::tags::hot_target>(), expected.hot_target.at(index));
        boost::ut::expect(row.template get<fieldpack_benchmark::tags::cold_0>() == expected.cold[0].at(index));
        boost::ut::expect(row.template get<fieldpack_benchmark::tags::cold_1>() == expected.cold[1].at(index));
        boost::ut::expect(row.template get<fieldpack_benchmark::tags::cold_2>() == expected.cold[2].at(index));
        boost::ut::expect(row.template get<fieldpack_benchmark::tags::cold_3>() == expected.cold[3].at(index));
        boost::ut::expect(row.template get<fieldpack_benchmark::tags::cold_4>() == expected.cold[4].at(index));
        boost::ut::expect(row.template get<fieldpack_benchmark::tags::cold_5>() == expected.cold[5].at(index));
    }
}

template<class Layout>
void expect_reduction_table_unchanged(const fieldpack::table<fieldpack_benchmark::reduction_schema, Layout>& actual,
                                      const fieldpack_benchmark::reduction_arrays& expected)
{
    boost::ut::expect(actual.size() == expected.a.size());
    for (std::size_t index = 0U; index < actual.size(); ++index) {
        const auto row = actual[index];
        boost::ut::expect(row.template get<fieldpack_benchmark::tags::reduction_a>() == expected.a.at(index));
        boost::ut::expect(row.template get<fieldpack_benchmark::tags::reduction_b>() == expected.b.at(index));
        boost::ut::expect(row.template get<fieldpack_benchmark::tags::reduction_c>() == expected.c.at(index));
        boost::ut::expect(row.template get<fieldpack_benchmark::tags::reduction_d>() == expected.d.at(index));
    }
}

template<class Layout> void check_polynomial(std::size_t size)
{
    auto expected = fieldpack_benchmark::make_polynomial_arrays(size);
    auto raw = expected;
    fieldpack::table<fieldpack_benchmark::polynomial_schema, Layout> values(size);
    fieldpack_benchmark::initialize(values);

    reference_polynomial(expected);
    fieldpack_benchmark::polynomial_evaluation(raw);
    fieldpack_benchmark::polynomial_evaluation(values);

    expect_polynomial_raw(raw, expected);
    expect_polynomial_table(values, expected);
}

template<class Layout> void check_drift(std::size_t size)
{
    auto expected = fieldpack_benchmark::make_drift_arrays(size);
    auto raw = expected;
    fieldpack::table<fieldpack_benchmark::drift_schema, Layout> values(size);
    fieldpack_benchmark::initialize(values);

    reference_drift(expected);
    fieldpack_benchmark::drift(raw, time_step);
    fieldpack_benchmark::drift(values, time_step);

    expect_drift_raw(raw, expected);
    expect_drift_table(values, expected);
}

template<class Layout> void check_field_subset(std::size_t size)
{
    auto expected = fieldpack_benchmark::make_field_subset_arrays(size);
    auto raw = expected;
    fieldpack::table<fieldpack_benchmark::field_subset_schema, Layout> values(size);
    fieldpack_benchmark::initialize(values);

    reference_field_subset(expected);
    fieldpack_benchmark::field_subset(raw, subset_scale);
    fieldpack_benchmark::field_subset(values, subset_scale);

    expect_field_subset_raw(raw, expected);
    expect_field_subset_table(values, expected);
}

template<class Layout> void check_reduction(std::size_t size)
{
    const auto expected = fieldpack_benchmark::make_reduction_arrays(size);
    // The copy is intentional: the read-only raw kernel must leave every
    // source array unchanged, which is verified after calculating its result.
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    auto raw = expected;
    fieldpack::table<fieldpack_benchmark::reduction_schema, Layout> values(size);
    fieldpack_benchmark::initialize(values);

    const auto reference_checksum = reference_reduction(expected);
    const auto raw_checksum = fieldpack_benchmark::reduction(raw);
    const auto table_checksum = fieldpack_benchmark::reduction(static_cast<const decltype(values)&>(values));

    expect_reduction_close(raw_checksum, reference_checksum, expected);
    expect_reduction_close(table_checksum, reference_checksum, expected);
    boost::ut::expect(raw.a == expected.a);
    boost::ut::expect(raw.b == expected.b);
    boost::ut::expect(raw.c == expected.c);
    boost::ut::expect(raw.d == expected.d);
    expect_reduction_table_unchanged(values, expected);
}

template<class Layout> void check_layout(std::size_t size)
{
    check_polynomial<Layout>(size);
    check_drift<Layout>(size);
    check_field_subset<Layout>(size);
    check_reduction<Layout>(size);
}

void check_all_layouts(std::size_t size)
{
    check_layout<fieldpack::soa>(size);
    check_layout<fieldpack::aosoa<16U>>(size);
    check_layout<fieldpack::aosoa<32U>>(size);
    check_layout<fieldpack::aosoa<64U>>(size);
    check_layout<fieldpack::aosoa<128U>>(size);
}

template<std::size_t TileExtent, class Schema> struct generated_schema_tile;

template<std::size_t TileExtent, class First, class... Rest>
struct generated_schema_tile<TileExtent, fieldpack::schema<First, Rest...>> {
    using first_tag = fieldpack::detail::field_traits<First>::tag;
    using type = fieldpack::detail::tile_storage<TileExtent, First, Rest...>;
};

template<class Schema, std::size_t TileExtent> void check_generated_backend_control_flow()
{
    using generated = generated_schema_tile<TileExtent, Schema>;
    using tile_type = generated::type;
    using first_tag = generated::first_tag;
    using allocator_type = fieldpack::detail::aligned_allocator<tile_type>;
    using storage_type = fieldpack::detail::aosoa_storage<Schema, TileExtent>;

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

    storage_type values((2U * TileExtent) + 1U);
    boost::ut::expect(!values.empty());
    boost::ut::expect(values.physical_tile_count() == 3U);
    boost::ut::expect(values.contiguous_count(0U) == TileExtent);
    boost::ut::expect(values.contiguous_count(2U * TileExtent) == 1U);
    boost::ut::expect(values.contiguous_count(values.size()) == 0U);

    auto empty_span = values.template contiguous_span<first_tag>(values.size(), 0U);
    boost::ut::expect(empty_span.empty());
    const auto& observed = values;
    auto empty_const_span = observed.template contiguous_span<first_tag>(observed.size(), 0U);
    boost::ut::expect(empty_const_span.empty());

    values.resize(values.size());
    values.resize(2U * TileExtent);
    values.resize(TileExtent + 1U);
    values.resize(TileExtent + 2U);
    values.resize(2U * TileExtent);
    values.resize((2U * TileExtent) + 1U);

    storage_type copied(values);
    storage_type copy_assigned;
    copy_assigned = copied;
    auto* copy_assigned_alias = std::addressof(copy_assigned);
    copy_assigned = *copy_assigned_alias;
    storage_type moved(std::move(copied));
    storage_type move_assigned;
    move_assigned = std::move(moved);
    auto* move_assigned_alias = std::addressof(move_assigned);
    move_assigned = std::move(*move_assigned_alias);
    boost::ut::expect(move_assigned.size() == (2U * TileExtent) + 1U);

    values.resize(0U);
    boost::ut::expect(values.empty());
    values.resize(TileExtent);
    boost::ut::expect(values.size() == TileExtent);

    boost::ut::expect(boost::ut::throws<std::bad_array_new_length>(
        [] { static_cast<void>(storage_type{std::numeric_limits<std::size_t>::max()}); }));
}

template<class Schema> void check_schema_backend_control_flow()
{
    check_generated_backend_control_flow<Schema, 16U>();
    check_generated_backend_control_flow<Schema, 32U>();
    check_generated_backend_control_flow<Schema, 64U>();
    check_generated_backend_control_flow<Schema, 128U>();
}

} // namespace

int main() // NOLINT(bugprone-exception-escape) -- Boost.UT owns top-level test exception handling
{
    using namespace boost::ut;

    static_assert(std::same_as<fieldpack::field_type_t<fieldpack_benchmark::polynomial_schema,
                                                       fieldpack_benchmark::tags::polynomial_output>,
                               double>);
    static_assert(
        std::same_as<fieldpack::field_type_t<fieldpack_benchmark::drift_schema, fieldpack_benchmark::tags::position_x>,
                     double>);
    static_assert(
        std::same_as<
            fieldpack::field_type_t<fieldpack_benchmark::field_subset_schema, fieldpack_benchmark::tags::hot_target>,
            double>);
    static_assert(
        std::same_as<
            fieldpack::field_type_t<fieldpack_benchmark::reduction_schema, fieldpack_benchmark::tags::reduction_a>,
            double>);

    "benchmark kernels match independent references across every layout boundary"_test = [] {
        constexpr std::size_t largest_tile_extent = 128U;
        for (std::size_t size = 0U; size <= (2U * largest_tile_extent) + fieldpack_benchmark::chunk_extent; ++size) {
            check_all_layouts(size);
        }
    };

    "benchmark problem sizes remain complete ordered and unique"_test = [] {
        constexpr std::array<std::size_t, 6> expected{63U, 64U, 65U, 1'024U, 32'768U, 1'048'576U};
        constexpr auto actual = fieldpack_benchmark::problem_sizes();
        static_assert(actual == expected);
        expect(actual == expected);
        expect(std::ranges::is_sorted(actual));
        expect(std::ranges::adjacent_find(actual) == actual.end());
    };

    "benchmark layout labels remain complete ordered and stable"_test = [] {
        constexpr std::array<std::string_view, 5> expected{"soa", "aosoa<16>", "aosoa<32>", "aosoa<64>", "aosoa<128>"};
        constexpr auto actual = fieldpack_benchmark::layout_labels();
        static_assert(actual == expected);
        expect(actual == expected);
    };

    "benchmark schemas cover their generated AoSoA backend control flow"_test = [] {
        check_schema_backend_control_flow<fieldpack_benchmark::polynomial_schema>();
        check_schema_backend_control_flow<fieldpack_benchmark::drift_schema>();
        check_schema_backend_control_flow<fieldpack_benchmark::field_subset_schema>();
        check_schema_backend_control_flow<fieldpack_benchmark::reduction_schema>();
    };
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
