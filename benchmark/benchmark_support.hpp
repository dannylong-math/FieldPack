#pragma once // NOLINT(portability-avoid-pragma-once) -- project-wide header convention

#include <array>
#include <cstddef>
#include <cstdint>
#include <fieldpack/fieldpack.hpp>
#include <string_view>
#include <vector>

/**
 * @file benchmark_support.hpp
 * @brief Timing-independent kernels, inputs, and matrix metadata for benchmarks.
 *
 * This header deliberately has no Google Benchmark dependency. Unit tests call
 * the same kernels that benchmark executables will time, while keeping scalar
 * reference implementations independent in the test suite.
 */

namespace fieldpack_benchmark {

/** @brief Fixed logical chunk extent used by the initial benchmark suite. */
inline constexpr std::size_t chunk_extent = 8U;

/** @brief Return the ordered problem-size matrix used by every benchmark. */
[[nodiscard]] constexpr auto problem_sizes() noexcept -> std::array<std::size_t, 6>
{
    return {63U, 64U, 65U, 1U << 10U, 1U << 15U, 1U << 20U};
}

/** @brief Map a benchmarked layout type to its stable result label. */
template<class Layout> struct layout_label;

/** @brief Stable label for structure-of-arrays results. */
template<> struct layout_label<fieldpack::soa> {
    static constexpr std::string_view value = "soa";
};

/** @brief Stable label for array-of-structures-of-arrays results. */
template<std::size_t TileExtent> struct layout_label<fieldpack::aosoa<TileExtent>> {
    static constexpr auto value = []() consteval {
        if constexpr (TileExtent == 16U) {
            return std::string_view{"aosoa<16>"};
        }
        else if constexpr (TileExtent == 32U) {
            return std::string_view{"aosoa<32>"};
        }
        else if constexpr (TileExtent == 64U) {
            return std::string_view{"aosoa<64>"};
        }
        else if constexpr (TileExtent == 128U) {
            return std::string_view{"aosoa<128>"};
        }
        else {
            static_assert(TileExtent == 16U || TileExtent == 32U || TileExtent == 64U || TileExtent == 128U,
                          "layout_label is defined only for benchmarked AoSoA tile extents");
            return std::string_view{};
        }
    }();
};

/** @brief Return the ordered labels corresponding to the benchmark layouts. */
[[nodiscard]] constexpr auto layout_labels() noexcept -> std::array<std::string_view, 5>
{
    return {layout_label<fieldpack::soa>::value, layout_label<fieldpack::aosoa<16U>>::value,
            layout_label<fieldpack::aosoa<32U>>::value, layout_label<fieldpack::aosoa<64U>>::value,
            layout_label<fieldpack::aosoa<128U>>::value};
}

namespace tags {

struct coefficient_0 {};
struct coefficient_1 {};
struct coefficient_2 {};
struct coefficient_3 {};
struct polynomial_input {};
struct polynomial_output {};

struct position_x {};
struct position_y {};
struct velocity_x {};
struct velocity_y {};

struct hot_source {};
struct hot_target {};
struct cold_0 {};
struct cold_1 {};
struct cold_2 {};
struct cold_3 {};
struct cold_4 {};
struct cold_5 {};

struct reduction_a {};
struct reduction_b {};
struct reduction_c {};
struct reduction_d {};

} // namespace tags

/** @brief Schema used by the Horner polynomial kernel. */
using polynomial_schema =
    fieldpack::schema<fieldpack::field<tags::coefficient_0, double>, fieldpack::field<tags::coefficient_1, double>,
                      fieldpack::field<tags::coefficient_2, double>, fieldpack::field<tags::coefficient_3, double>,
                      fieldpack::field<tags::polynomial_input, double>,
                      fieldpack::field<tags::polynomial_output, double>>;

/** @brief Schema used by the two-dimensional drift kernel. */
using drift_schema =
    fieldpack::schema<fieldpack::field<tags::position_x, double>, fieldpack::field<tags::velocity_y, double>,
                      fieldpack::field<tags::position_y, double>, fieldpack::field<tags::velocity_x, double>>;

/** @brief Wide schema used to expose two-hot-field bandwidth effects. */
using field_subset_schema =
    fieldpack::schema<fieldpack::field<tags::cold_0, double>, fieldpack::field<tags::hot_source, double>,
                      fieldpack::field<tags::cold_1, double>, fieldpack::field<tags::cold_2, double>,
                      fieldpack::field<tags::hot_target, double>, fieldpack::field<tags::cold_3, double>,
                      fieldpack::field<tags::cold_4, double>, fieldpack::field<tags::cold_5, double>>;

/** @brief Schema used by the read-only checksum kernel. */
using reduction_schema =
    fieldpack::schema<fieldpack::field<tags::reduction_a, double>, fieldpack::field<tags::reduction_b, double>,
                      fieldpack::field<tags::reduction_c, double>, fieldpack::field<tags::reduction_d, double>>;

/** @brief Raw structure-of-arrays storage for the polynomial baseline. */
struct polynomial_arrays {
    std::vector<double> coefficient_0;
    std::vector<double> coefficient_1;
    std::vector<double> coefficient_2;
    std::vector<double> coefficient_3;
    std::vector<double> input;
    std::vector<double> output;
};

/** @brief Raw structure-of-arrays storage for the drift baseline. */
struct drift_arrays {
    std::vector<double> position_x;
    std::vector<double> position_y;
    std::vector<double> velocity_x;
    std::vector<double> velocity_y;
};

/** @brief Raw structure-of-arrays storage for the field-subset baseline. */
struct field_subset_arrays {
    std::vector<double> hot_source;
    std::vector<double> hot_target;
    std::array<std::vector<double>, 6> cold;
};

/** @brief Raw structure-of-arrays storage for the reduction baseline. */
struct reduction_arrays {
    std::vector<double> a;
    std::vector<double> b;
    std::vector<double> c;
    std::vector<double> d;
};

/** @brief One deterministic polynomial input row. */
struct polynomial_record {
    double coefficient_0;
    double coefficient_1;
    double coefficient_2;
    double coefficient_3;
    double input;
    double output;
};

/** @brief One deterministic drift input row. */
struct drift_record {
    double position_x;
    double position_y;
    double velocity_x;
    double velocity_y;
};

/** @brief One deterministic field-subset input row. */
struct field_subset_record {
    double hot_source;
    double hot_target;
    std::array<double, 6> cold;
};

/** @brief One deterministic reduction input row. */
struct reduction_record {
    double a;
    double b;
    double c;
    double d;
};

/** @brief Strong stream identifier preventing index/stream argument swaps. */
struct input_stream {
    std::uint64_t value;
};

/** @brief Produce a reproducible moderate finite value for a row and stream. */
[[nodiscard]] constexpr auto generated_value(std::size_t index, input_stream stream) noexcept -> double
{
    auto mixed = (static_cast<std::uint64_t>(index) + 1U) * 0x9E3779B97F4A7C15ULL;
    mixed ^= stream.value * 0xD1B54A32D192ED03ULL;
    mixed ^= mixed >> 29U;
    mixed *= 0x94D049BB133111EBULL;
    mixed ^= mixed >> 31U;
    const auto magnitude = static_cast<std::int64_t>(mixed % 4'097U) - 2'048;
    return static_cast<double>(magnitude) / 64.0;
}

/** @brief Generate polynomial inputs including focused finite edge cases. */
[[nodiscard]] constexpr auto polynomial_input(std::size_t index) noexcept -> polynomial_record
{
    switch (index) {
    case 0U:
        return {};
    case 1U:
        return {.coefficient_0 = -3.5,
                .coefficient_1 = 0.25,
                .coefficient_2 = -0.125,
                .coefficient_3 = 2.0,
                .input = -0.5,
                .output = 7.0};
    case 2U:
        return {.coefficient_0 = 1.0e100,
                .coefficient_1 = -1.0e50,
                .coefficient_2 = 1.0e20,
                .coefficient_3 = -1.0e10,
                .input = 1.0e-10,
                .output = -1.0e80};
    case 3U:
        return {.coefficient_0 = 1.0e-100,
                .coefficient_1 = -1.0e-90,
                .coefficient_2 = 1.0e-80,
                .coefficient_3 = -1.0e-70,
                .input = 0.5,
                .output = 1.0e-60};
    default:
        return {.coefficient_0 = generated_value(index, input_stream{0U}),
                .coefficient_1 = generated_value(index, input_stream{1U}),
                .coefficient_2 = generated_value(index, input_stream{2U}),
                .coefficient_3 = generated_value(index, input_stream{3U}),
                .input = generated_value(index, input_stream{4U}) / 8.0,
                .output = generated_value(index, input_stream{5U})};
    }
}

/** @brief Generate drift inputs including focused finite edge cases. */
[[nodiscard]] constexpr auto drift_input(std::size_t index) noexcept -> drift_record
{
    switch (index) {
    case 0U:
        return {};
    case 1U:
        return {.position_x = -3.5, .position_y = 0.25, .velocity_x = -0.125, .velocity_y = 2.0};
    case 2U:
        return {.position_x = 1.0e100, .position_y = -1.0e100, .velocity_x = 1.0e90, .velocity_y = -1.0e90};
    case 3U:
        return {.position_x = 1.0e-100, .position_y = -1.0e-100, .velocity_x = 1.0e-110, .velocity_y = -1.0e-110};
    default:
        return {.position_x = generated_value(index, input_stream{6U}),
                .position_y = generated_value(index, input_stream{7U}),
                .velocity_x = generated_value(index, input_stream{8U}) / 8.0,
                .velocity_y = generated_value(index, input_stream{9U}) / 8.0};
    }
}

/** @brief Generate field-subset inputs and untouched cold-field sentinels. */
[[nodiscard]] constexpr auto field_subset_input(std::size_t index) noexcept -> field_subset_record
{
    if (index == 0U) {
        return {};
    }
    if (index == 1U) {
        return {.hot_source = -3.5, .hot_target = 0.25, .cold = {-1.0, 0.0, 0.125, -0.5, 1.0e-100, 1.0e100}};
    }
    std::array<double, 6> cold{};
    for (std::size_t field = 0U; field < cold.size(); ++field) {
        cold.at(field) = generated_value(index, input_stream{20U + field});
    }
    return {.hot_source = generated_value(index, input_stream{10U}),
            .hot_target = generated_value(index, input_stream{11U}),
            .cold = cold};
}

/** @brief Generate read-only reduction inputs including finite edge cases. */
[[nodiscard]] constexpr auto reduction_input(std::size_t index) noexcept -> reduction_record
{
    switch (index) {
    case 0U:
        return {};
    case 1U:
        return {.a = -3.5, .b = 0.25, .c = -0.125, .d = 2.0};
    case 2U:
        return {.a = 1.0e100, .b = -1.0e100, .c = 1.0e90, .d = -1.0e90};
    case 3U:
        return {.a = 1.0e-100, .b = -1.0e-100, .c = 1.0e-110, .d = -1.0e-110};
    default:
        return {.a = generated_value(index, input_stream{30U}),
                .b = generated_value(index, input_stream{31U}),
                .c = generated_value(index, input_stream{32U}),
                .d = generated_value(index, input_stream{33U})};
    }
}

// Initialization and kernel loops are bounded by the table, vector, or chunk
// size established in the same function. Unchecked access is intentional in
// timed kernels so the baseline does not include bounds-check overhead.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index)

/** @brief Create and initialize the raw polynomial baseline arrays. */
[[nodiscard]] inline auto make_polynomial_arrays(std::size_t count) -> polynomial_arrays
{
    polynomial_arrays result{.coefficient_0 = std::vector<double>(count),
                             .coefficient_1 = std::vector<double>(count),
                             .coefficient_2 = std::vector<double>(count),
                             .coefficient_3 = std::vector<double>(count),
                             .input = std::vector<double>(count),
                             .output = std::vector<double>(count)};
    for (std::size_t index = 0U; index < count; ++index) {
        const auto record = polynomial_input(index);
        result.coefficient_0[index] = record.coefficient_0;
        result.coefficient_1[index] = record.coefficient_1;
        result.coefficient_2[index] = record.coefficient_2;
        result.coefficient_3[index] = record.coefficient_3;
        result.input[index] = record.input;
        result.output[index] = record.output;
    }
    return result;
}

/** @brief Create and initialize the raw drift baseline arrays. */
[[nodiscard]] inline auto make_drift_arrays(std::size_t count) -> drift_arrays
{
    drift_arrays result{.position_x = std::vector<double>(count),
                        .position_y = std::vector<double>(count),
                        .velocity_x = std::vector<double>(count),
                        .velocity_y = std::vector<double>(count)};
    for (std::size_t index = 0U; index < count; ++index) {
        const auto record = drift_input(index);
        result.position_x[index] = record.position_x;
        result.position_y[index] = record.position_y;
        result.velocity_x[index] = record.velocity_x;
        result.velocity_y[index] = record.velocity_y;
    }
    return result;
}

/** @brief Create and initialize the raw field-subset baseline arrays. */
[[nodiscard]] inline auto make_field_subset_arrays(std::size_t count) -> field_subset_arrays
{
    field_subset_arrays result{.hot_source = std::vector<double>(count),
                               .hot_target = std::vector<double>(count),
                               .cold = {std::vector<double>(count), std::vector<double>(count),
                                        std::vector<double>(count), std::vector<double>(count),
                                        std::vector<double>(count), std::vector<double>(count)}};
    for (std::size_t index = 0U; index < count; ++index) {
        const auto record = field_subset_input(index);
        result.hot_source[index] = record.hot_source;
        result.hot_target[index] = record.hot_target;
        for (std::size_t field = 0U; field < result.cold.size(); ++field) {
            result.cold[field][index] = record.cold[field];
        }
    }
    return result;
}

/** @brief Create and initialize the raw reduction baseline arrays. */
[[nodiscard]] inline auto make_reduction_arrays(std::size_t count) -> reduction_arrays
{
    reduction_arrays result{.a = std::vector<double>(count),
                            .b = std::vector<double>(count),
                            .c = std::vector<double>(count),
                            .d = std::vector<double>(count)};
    for (std::size_t index = 0U; index < count; ++index) {
        const auto record = reduction_input(index);
        result.a[index] = record.a;
        result.b[index] = record.b;
        result.c[index] = record.c;
        result.d[index] = record.d;
    }
    return result;
}

/** @brief Initialize a FieldPack polynomial table with deterministic inputs. */
template<class Layout> inline void initialize(fieldpack::table<polynomial_schema, Layout>& values)
{
    for (std::size_t index = 0U; index < values.size(); ++index) {
        const auto record = polynomial_input(index);
        auto row = values[index];
        row.template get<tags::coefficient_0>() = record.coefficient_0;
        row.template get<tags::coefficient_1>() = record.coefficient_1;
        row.template get<tags::coefficient_2>() = record.coefficient_2;
        row.template get<tags::coefficient_3>() = record.coefficient_3;
        row.template get<tags::polynomial_input>() = record.input;
        row.template get<tags::polynomial_output>() = record.output;
    }
}

/** @brief Initialize a FieldPack drift table with deterministic inputs. */
template<class Layout> inline void initialize(fieldpack::table<drift_schema, Layout>& values)
{
    for (std::size_t index = 0U; index < values.size(); ++index) {
        const auto record = drift_input(index);
        auto row = values[index];
        row.template get<tags::position_x>() = record.position_x;
        row.template get<tags::position_y>() = record.position_y;
        row.template get<tags::velocity_x>() = record.velocity_x;
        row.template get<tags::velocity_y>() = record.velocity_y;
    }
}

/** @brief Initialize a FieldPack field-subset table with deterministic inputs. */
template<class Layout> inline void initialize(fieldpack::table<field_subset_schema, Layout>& values)
{
    for (std::size_t index = 0U; index < values.size(); ++index) {
        const auto record = field_subset_input(index);
        auto row = values[index];
        row.template get<tags::hot_source>() = record.hot_source;
        row.template get<tags::hot_target>() = record.hot_target;
        row.template get<tags::cold_0>() = record.cold[0];
        row.template get<tags::cold_1>() = record.cold[1];
        row.template get<tags::cold_2>() = record.cold[2];
        row.template get<tags::cold_3>() = record.cold[3];
        row.template get<tags::cold_4>() = record.cold[4];
        row.template get<tags::cold_5>() = record.cold[5];
    }
}

/** @brief Initialize a FieldPack reduction table with deterministic inputs. */
template<class Layout> inline void initialize(fieldpack::table<reduction_schema, Layout>& values)
{
    for (std::size_t index = 0U; index < values.size(); ++index) {
        const auto record = reduction_input(index);
        auto row = values[index];
        row.template get<tags::reduction_a>() = record.a;
        row.template get<tags::reduction_b>() = record.b;
        row.template get<tags::reduction_c>() = record.c;
        row.template get<tags::reduction_d>() = record.d;
    }
}

/** @brief Evaluate one cubic polynomial per row using Horner's method. */
template<class Layout> inline void polynomial_evaluation(fieldpack::table<polynomial_schema, Layout>& values)
{
    using access =
        fieldpack::field_access<fieldpack::read<tags::coefficient_0>, fieldpack::read<tags::coefficient_1>,
                                fieldpack::read<tags::coefficient_2>, fieldpack::read<tags::coefficient_3>,
                                fieldpack::read<tags::polynomial_input>, fieldpack::mutate<tags::polynomial_output>>;
    fieldpack::for_each_chunk<chunk_extent>(values, access{}, [](auto fields) {
        const auto coefficient_0 = fields.template get<tags::coefficient_0>();
        const auto coefficient_1 = fields.template get<tags::coefficient_1>();
        const auto coefficient_2 = fields.template get<tags::coefficient_2>();
        const auto coefficient_3 = fields.template get<tags::coefficient_3>();
        const auto input = fields.template get<tags::polynomial_input>();
        auto output = fields.template get<tags::polynomial_output>();
        for (std::size_t lane = 0U; lane < fields.size(); ++lane) {
            auto result = coefficient_3[lane];
            result = (result * input[lane]) + coefficient_2[lane];
            result = (result * input[lane]) + coefficient_1[lane];
            result = (result * input[lane]) + coefficient_0[lane];
            output[lane] = result;
        }
    });
}

/** @brief Advance two position fields from their velocities and a time step. */
template<class Layout> inline void drift(fieldpack::table<drift_schema, Layout>& values, double time_step)
{
    using access = fieldpack::field_access<fieldpack::mutate<tags::position_x>, fieldpack::mutate<tags::position_y>,
                                           fieldpack::read<tags::velocity_x>, fieldpack::read<tags::velocity_y>>;
    fieldpack::for_each_chunk<chunk_extent>(values, access{}, [time_step](auto fields) {
        auto position_x = fields.template get<tags::position_x>();
        auto position_y = fields.template get<tags::position_y>();
        const auto velocity_x = fields.template get<tags::velocity_x>();
        const auto velocity_y = fields.template get<tags::velocity_y>();
        for (std::size_t lane = 0U; lane < fields.size(); ++lane) {
            position_x[lane] += time_step * velocity_x[lane];
            position_y[lane] += time_step * velocity_y[lane];
        }
    });
}

/** @brief Update two hot fields while leaving six cold fields untouched. */
template<class Layout> inline void field_subset(fieldpack::table<field_subset_schema, Layout>& values, double scale)
{
    using access = fieldpack::field_access<fieldpack::read<tags::hot_source>, fieldpack::mutate<tags::hot_target>>;
    fieldpack::for_each_chunk<chunk_extent>(values, access{}, [scale](auto fields) {
        const auto source = fields.template get<tags::hot_source>();
        auto target = fields.template get<tags::hot_target>();
        for (std::size_t lane = 0U; lane < fields.size(); ++lane) {
            target[lane] += scale * source[lane];
        }
    });
}

/** @brief Compute a read-only checksum with explicit row-major accumulation. */
template<class Layout>
[[nodiscard]] inline auto reduction(const fieldpack::table<reduction_schema, Layout>& values) -> double
{
    using access = fieldpack::field_access<fieldpack::read<tags::reduction_a>, fieldpack::read<tags::reduction_b>,
                                           fieldpack::read<tags::reduction_c>, fieldpack::read<tags::reduction_d>>;
    double checksum = 0.0;
    fieldpack::for_each_chunk<chunk_extent>(values, access{}, [&checksum](auto fields) {
        const auto values_a = fields.template get<tags::reduction_a>();
        const auto values_b = fields.template get<tags::reduction_b>();
        const auto values_c = fields.template get<tags::reduction_c>();
        const auto values_d = fields.template get<tags::reduction_d>();
        for (std::size_t lane = 0U; lane < fields.size(); ++lane) {
            // The nonzero row marker makes accidental zero-initialized padding
            // reads observable in the independently calculated checksum.
            checksum += 1.0;
            checksum += values_a[lane];
            checksum += values_b[lane];
            checksum += values_c[lane];
            checksum += values_d[lane];
        }
    });
    return checksum;
}

/** @brief Raw structure-of-arrays baseline for polynomial evaluation. */
inline void polynomial_evaluation(polynomial_arrays& values)
{
    for (std::size_t index = 0U; index < values.output.size(); ++index) {
        const auto input = values.input[index];
        auto result = values.coefficient_3[index];
        result = (result * input) + values.coefficient_2[index];
        result = (result * input) + values.coefficient_1[index];
        result = (result * input) + values.coefficient_0[index];
        values.output[index] = result;
    }
}

/** @brief Raw structure-of-arrays baseline for drift. */
inline void drift(drift_arrays& values, double time_step)
{
    for (std::size_t index = 0U; index < values.position_x.size(); ++index) {
        values.position_x[index] += time_step * values.velocity_x[index];
        values.position_y[index] += time_step * values.velocity_y[index];
    }
}

/** @brief Raw structure-of-arrays baseline for the two-hot-field update. */
inline void field_subset(field_subset_arrays& values, double scale)
{
    for (std::size_t index = 0U; index < values.hot_target.size(); ++index) {
        values.hot_target[index] += scale * values.hot_source[index];
    }
}

/** @brief Raw structure-of-arrays baseline for the read-only checksum. */
[[nodiscard]] inline auto reduction(const reduction_arrays& values) -> double
{
    double checksum = 0.0;
    for (std::size_t index = 0U; index < values.a.size(); ++index) {
        checksum += 1.0;
        checksum += values.a[index];
        checksum += values.b[index];
        checksum += values.c[index];
        checksum += values.d[index];
    }
    return checksum;
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index)

} // namespace fieldpack_benchmark
