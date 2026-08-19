#include <algorithm>
#include <array>
#include <boost/ut.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fieldpack/detail/aligned_allocator.hpp>
#include <fieldpack/detail/aosoa_storage.hpp>
#include <fieldpack/execution.hpp>
#include <fieldpack/layout.hpp>
#include <fieldpack/schema.hpp>
#include <fieldpack/table.hpp>
#include <limits>
#include <memory>
#include <new>
#include <utility>
#include <vector>

// Every unchecked access in this integration suite is bounded by either the
// owning vector/table size or the chunk size supplied by FieldPack.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

namespace {

inline constexpr std::size_t tile_extent = 64U;
inline constexpr std::size_t chunk_extent = 8U;

class deterministic_generator {
public:
    explicit constexpr deterministic_generator(std::uint64_t seed) noexcept : state_(seed) {}

    [[nodiscard]] constexpr auto next() noexcept -> std::uint32_t
    {
        state_ = (state_ * 6'364'136'223'846'793'005ULL) + 1'442'695'040'888'963'407ULL;
        return static_cast<std::uint32_t>(state_ >> 32U);
    }

    [[nodiscard]] constexpr auto signed_value(std::int32_t magnitude) noexcept -> std::int32_t
    {
        const auto range = static_cast<std::uint32_t>((2 * magnitude) + 1);
        return static_cast<std::int32_t>(next() % range) - magnitude;
    }

private:
    std::uint64_t state_;
};

struct source {};
struct target {};
struct position_x {};
struct position_y {};
struct velocity_x {};
struct velocity_y {};
struct coefficient_0 {};
struct coefficient_1 {};
struct coefficient_2 {};
struct coefficient_3 {};
struct polynomial_input {};
struct polynomial_output {};
struct unused_float {};
struct unused_identifier {};

using floating_schema =
    fieldpack::schema<fieldpack::field<unused_identifier, std::uint32_t>, fieldpack::field<velocity_y, double>,
                      fieldpack::field<source, float>, fieldpack::field<coefficient_2, double>,
                      fieldpack::field<position_x, double>, fieldpack::field<target, float>,
                      fieldpack::field<polynomial_output, double>, fieldpack::field<coefficient_0, double>,
                      fieldpack::field<unused_float, float>, fieldpack::field<velocity_x, double>,
                      fieldpack::field<coefficient_3, double>, fieldpack::field<position_y, double>,
                      fieldpack::field<polynomial_input, double>, fieldpack::field<coefficient_1, double>>;

struct floating_record {
    float source_value{};
    float target_value{};
    double position_x_value{};
    double position_y_value{};
    double velocity_x_value{};
    double velocity_y_value{};
    double coefficient_0_value{};
    double coefficient_1_value{};
    double coefficient_2_value{};
    double coefficient_3_value{};
    double polynomial_input_value{};
    double polynomial_output_value{};
    float unused_float_value{};
    std::uint32_t unused_identifier_value{};
};

struct integer_source {};
struct integer_target {};
struct integer_unused {};

using integer_schema =
    fieldpack::schema<fieldpack::field<integer_unused, std::uint32_t>, fieldpack::field<integer_target, std::int64_t>,
                      fieldpack::field<integer_source, std::int32_t>>;

struct integer_record {
    std::int32_t source_value{};
    std::int64_t target_value{};
    std::uint32_t unused_value{};

    auto operator==(const integer_record&) const -> bool = default;
};

struct generated_case {
    std::size_t logical_size;
    std::uint64_t seed;
};

[[nodiscard]] auto make_floating_record(std::uint64_t seed, std::size_t index) -> floating_record
{
    deterministic_generator generator(seed ^ ((static_cast<std::uint64_t>(index) + 1U) * 0x9E3779B97F4A7C15ULL));
    return {
        .source_value = static_cast<float>(generator.signed_value(256)) / 16.0F,
        .target_value = static_cast<float>(generator.signed_value(256)) / 16.0F,
        .position_x_value = static_cast<double>(generator.signed_value(1'024)) / 32.0,
        .position_y_value = static_cast<double>(generator.signed_value(1'024)) / 32.0,
        .velocity_x_value = static_cast<double>(generator.signed_value(128)) / 64.0,
        .velocity_y_value = static_cast<double>(generator.signed_value(128)) / 64.0,
        .coefficient_0_value = static_cast<double>(generator.signed_value(128)) / 16.0,
        .coefficient_1_value = static_cast<double>(generator.signed_value(128)) / 16.0,
        .coefficient_2_value = static_cast<double>(generator.signed_value(128)) / 16.0,
        .coefficient_3_value = static_cast<double>(generator.signed_value(128)) / 16.0,
        .polynomial_input_value = static_cast<double>(generator.signed_value(128)) / 64.0,
        .polynomial_output_value = static_cast<double>(generator.signed_value(128)) / 16.0,
        .unused_float_value = static_cast<float>(generator.signed_value(512)) / 8.0F,
        .unused_identifier_value = generator.next(),
    };
}

[[nodiscard]] auto make_integer_record(std::uint64_t seed, std::size_t index) -> integer_record
{
    deterministic_generator generator(seed ^ ((static_cast<std::uint64_t>(index) + 1U) * 0xC0FFEE));
    return {
        .source_value = generator.signed_value(1'000),
        .target_value = generator.signed_value(10'000),
        .unused_value = generator.next(),
    };
}

template<class Table> void write_floating_record(Table& values, std::size_t index, const floating_record& record)
{
    auto row = values[index];
    row.template get<source>() = record.source_value;
    row.template get<target>() = record.target_value;
    row.template get<position_x>() = record.position_x_value;
    row.template get<position_y>() = record.position_y_value;
    row.template get<velocity_x>() = record.velocity_x_value;
    row.template get<velocity_y>() = record.velocity_y_value;
    row.template get<coefficient_0>() = record.coefficient_0_value;
    row.template get<coefficient_1>() = record.coefficient_1_value;
    row.template get<coefficient_2>() = record.coefficient_2_value;
    row.template get<coefficient_3>() = record.coefficient_3_value;
    row.template get<polynomial_input>() = record.polynomial_input_value;
    row.template get<polynomial_output>() = record.polynomial_output_value;
    row.template get<unused_float>() = record.unused_float_value;
    row.template get<unused_identifier>() = record.unused_identifier_value;
}

template<class Table> void write_integer_record(Table& values, std::size_t index, const integer_record& record)
{
    auto row = values[index];
    row.template get<integer_source>() = record.source_value;
    row.template get<integer_target>() = record.target_value;
    row.template get<integer_unused>() = record.unused_value;
}

void expect_close(double actual, double expected)
{
    constexpr double absolute_tolerance = 1.0e-12;
    constexpr double relative_tolerance = 1.0e-12;
    const auto scale = std::max(std::abs(actual), std::abs(expected));
    boost::ut::expect(std::abs(actual - expected) <= absolute_tolerance + (relative_tolerance * scale));
}

template<class Table> void expect_floating_records(const Table& values, const std::vector<floating_record>& expected)
{
    boost::ut::expect(values.size() == expected.size());
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        const auto row = values[index];
        const auto& record = expected[index];
        expect_close(row.template get<source>(), record.source_value);
        expect_close(row.template get<target>(), record.target_value);
        expect_close(row.template get<position_x>(), record.position_x_value);
        expect_close(row.template get<position_y>(), record.position_y_value);
        expect_close(row.template get<velocity_x>(), record.velocity_x_value);
        expect_close(row.template get<velocity_y>(), record.velocity_y_value);
        expect_close(row.template get<coefficient_0>(), record.coefficient_0_value);
        expect_close(row.template get<coefficient_1>(), record.coefficient_1_value);
        expect_close(row.template get<coefficient_2>(), record.coefficient_2_value);
        expect_close(row.template get<coefficient_3>(), record.coefficient_3_value);
        expect_close(row.template get<polynomial_input>(), record.polynomial_input_value);
        expect_close(row.template get<polynomial_output>(), record.polynomial_output_value);
        expect_close(row.template get<unused_float>(), record.unused_float_value);
        boost::ut::expect(row.template get<unused_identifier>() == record.unused_identifier_value);
    }
}

template<class Table> void expect_integer_records(const Table& values, const std::vector<integer_record>& expected)
{
    boost::ut::expect(values.size() == expected.size());
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        const auto row = values[index];
        boost::ut::expect(row.template get<integer_source>() == expected[index].source_value);
        boost::ut::expect(row.template get<integer_target>() == expected[index].target_value);
        boost::ut::expect(row.template get<integer_unused>() == expected[index].unused_value);
    }
}

template<std::size_t ChunkExtent, class Table> void execute_copy(Table& values)
{
    using access = fieldpack::field_access<fieldpack::read<source>, fieldpack::mutate<target>>;
    fieldpack::for_each_chunk<ChunkExtent>(values, access{}, [](auto fields) {
        const auto sources = fields.template get<source>();
        auto targets = fields.template get<target>();
        for (std::size_t lane = 0U; lane < fields.size(); ++lane) {
            targets[lane] = sources[lane];
        }
    });
}

void reference_copy(std::vector<floating_record>& values)
{
    for (auto& value : values) {
        value.target_value = value.source_value;
    }
}

template<std::size_t ChunkExtent, class Table> void execute_fill(Table& values, float fill_value)
{
    using access = fieldpack::field_access<fieldpack::mutate<target>>;
    fieldpack::for_each_chunk<ChunkExtent>(values, access{}, [fill_value](auto fields) {
        auto targets = fields.template get<target>();
        for (std::size_t lane = 0U; lane < fields.size(); ++lane) {
            targets[lane] = fill_value;
        }
    });
}

void reference_fill(std::vector<floating_record>& values, float fill_value)
{
    for (auto& value : values) {
        value.target_value = fill_value;
    }
}

template<std::size_t ChunkExtent, class Table> void execute_scale(Table& values, float factor)
{
    using access = fieldpack::field_access<fieldpack::mutate<source>>;
    fieldpack::for_each_chunk<ChunkExtent>(values, access{}, [factor](auto fields) {
        auto sources = fields.template get<source>();
        for (std::size_t lane = 0U; lane < fields.size(); ++lane) {
            sources[lane] *= factor;
        }
    });
}

void reference_scale(std::vector<floating_record>& values, float factor)
{
    for (auto& value : values) {
        value.source_value *= factor;
    }
}

template<std::size_t ChunkExtent, class Table> void execute_axpy(Table& values, float alpha)
{
    using access = fieldpack::field_access<fieldpack::read<source>, fieldpack::mutate<target>>;
    fieldpack::for_each_chunk<ChunkExtent>(values, access{}, [alpha](auto fields) {
        const auto sources = fields.template get<source>();
        auto targets = fields.template get<target>();
        for (std::size_t lane = 0U; lane < fields.size(); ++lane) {
            targets[lane] += alpha * sources[lane];
        }
    });
}

void reference_axpy(std::vector<floating_record>& values, float alpha)
{
    for (auto& value : values) {
        value.target_value += alpha * value.source_value;
    }
}

template<std::size_t ChunkExtent, class Table> void execute_drift(Table& values, double time_step)
{
    using access = fieldpack::field_access<fieldpack::mutate<position_x>, fieldpack::read<velocity_y>,
                                           fieldpack::mutate<position_y>, fieldpack::read<velocity_x>>;
    fieldpack::for_each_chunk<ChunkExtent>(values, access{}, [time_step](auto fields) {
        auto positions_x = fields.template get<position_x>();
        const auto velocities_y = fields.template get<velocity_y>();
        auto positions_y = fields.template get<position_y>();
        const auto velocities_x = fields.template get<velocity_x>();
        for (std::size_t lane = 0U; lane < fields.size(); ++lane) {
            positions_x[lane] += time_step * velocities_x[lane];
            positions_y[lane] += time_step * velocities_y[lane];
        }
    });
}

void reference_drift(std::vector<floating_record>& values, double time_step)
{
    for (auto& value : values) {
        value.position_x_value += time_step * value.velocity_x_value;
        value.position_y_value += time_step * value.velocity_y_value;
    }
}

template<std::size_t ChunkExtent, class Table> void execute_polynomial(Table& values)
{
    using access = fieldpack::field_access<fieldpack::read<coefficient_0>, fieldpack::read<coefficient_1>,
                                           fieldpack::read<coefficient_2>, fieldpack::read<coefficient_3>,
                                           fieldpack::read<polynomial_input>, fieldpack::mutate<polynomial_output>>;
    fieldpack::for_each_chunk<ChunkExtent>(values, access{}, [](auto fields) {
        const auto coefficients_0 = fields.template get<coefficient_0>();
        const auto coefficients_1 = fields.template get<coefficient_1>();
        const auto coefficients_2 = fields.template get<coefficient_2>();
        const auto coefficients_3 = fields.template get<coefficient_3>();
        const auto inputs = fields.template get<polynomial_input>();
        auto outputs = fields.template get<polynomial_output>();
        for (std::size_t lane = 0U; lane < fields.size(); ++lane) {
            auto result = coefficients_3[lane];
            result = (result * inputs[lane]) + coefficients_2[lane];
            result = (result * inputs[lane]) + coefficients_1[lane];
            result = (result * inputs[lane]) + coefficients_0[lane];
            outputs[lane] = result;
        }
    });
}

void reference_polynomial(std::vector<floating_record>& values)
{
    for (auto& value : values) {
        auto result = value.coefficient_3_value;
        result = (result * value.polynomial_input_value) + value.coefficient_2_value;
        result = (result * value.polynomial_input_value) + value.coefficient_1_value;
        result = (result * value.polynomial_input_value) + value.coefficient_0_value;
        value.polynomial_output_value = result;
    }
}

template<std::size_t ChunkExtent, class Table> [[nodiscard]] auto execute_checksum(const Table& values) -> double
{
    using access =
        fieldpack::field_access<fieldpack::read<source>, fieldpack::read<target>, fieldpack::read<position_x>,
                                fieldpack::read<position_y>, fieldpack::read<polynomial_output>>;
    double checksum = 0.0;
    fieldpack::for_each_chunk<ChunkExtent>(values, access{}, [&checksum](auto fields) {
        const auto sources = fields.template get<source>();
        const auto targets = fields.template get<target>();
        const auto positions_x = fields.template get<position_x>();
        const auto positions_y = fields.template get<position_y>();
        const auto outputs = fields.template get<polynomial_output>();
        for (std::size_t lane = 0U; lane < fields.size(); ++lane) {
            // Preserve this field-by-field, row-major accumulation order in
            // the independent scalar reference below.
            checksum += sources[lane];
            checksum += targets[lane];
            checksum += positions_x[lane];
            checksum += positions_y[lane];
            checksum += outputs[lane];
        }
    });
    return checksum;
}

[[nodiscard]] auto reference_checksum(const std::vector<floating_record>& values) -> double
{
    double checksum = 0.0;
    for (const auto& value : values) {
        checksum += value.source_value;
        checksum += value.target_value;
        checksum += value.position_x_value;
        checksum += value.position_y_value;
        checksum += value.polynomial_output_value;
    }
    return checksum;
}

template<std::size_t ChunkExtent, class Table>
[[nodiscard]] auto execute_integer_checksum(const Table& values) -> std::int64_t
{
    using access = fieldpack::field_access<fieldpack::read<integer_source>, fieldpack::read<integer_target>>;
    std::int64_t checksum = 0;
    fieldpack::for_each_chunk<ChunkExtent>(values, access{}, [&checksum](auto fields) {
        const auto sources = fields.template get<integer_source>();
        const auto targets = fields.template get<integer_target>();
        for (std::size_t lane = 0U; lane < fields.size(); ++lane) {
            checksum += sources[lane];
            checksum += targets[lane];
        }
    });
    return checksum;
}

[[nodiscard]] auto reference_integer_checksum(const std::vector<integer_record>& values) -> std::int64_t
{
    std::int64_t checksum = 0;
    for (const auto& value : values) {
        checksum += value.source_value;
        checksum += value.target_value;
    }
    return checksum;
}

template<std::size_t ChunkExtent, class Layout> void check_floating_pipeline(generated_case test_case)
{
    fieldpack::table<floating_schema, Layout> values(test_case.logical_size);
    std::vector<floating_record> expected;
    expected.reserve(test_case.logical_size);
    for (std::size_t index = 0U; index < test_case.logical_size; ++index) {
        expected.push_back(make_floating_record(test_case.seed, index));
        write_floating_record(values, index, expected.back());
    }
    expect_floating_records(values, expected);

    reference_copy(expected);
    execute_copy<ChunkExtent>(values);
    expect_floating_records(values, expected);

    constexpr float fill_value = -3.25F;
    reference_fill(expected, fill_value);
    execute_fill<ChunkExtent>(values, fill_value);
    expect_floating_records(values, expected);

    constexpr float scale_factor = -0.5F;
    reference_scale(expected, scale_factor);
    execute_scale<ChunkExtent>(values, scale_factor);
    expect_floating_records(values, expected);

    constexpr float alpha = 0.25F;
    reference_axpy(expected, alpha);
    execute_axpy<ChunkExtent>(values, alpha);
    expect_floating_records(values, expected);

    constexpr double time_step = 0.125;
    reference_drift(expected, time_step);
    execute_drift<ChunkExtent>(values, time_step);
    expect_floating_records(values, expected);

    reference_polynomial(expected);
    execute_polynomial<ChunkExtent>(values);
    expect_floating_records(values, expected);

    const auto& observed = values;
    expect_close(execute_checksum<ChunkExtent>(observed), reference_checksum(expected));
}

template<std::size_t ChunkExtent, class Layout> void check_integer_pipeline(generated_case test_case)
{
    fieldpack::table<integer_schema, Layout> values(test_case.logical_size);
    std::vector<integer_record> expected;
    expected.reserve(test_case.logical_size);
    for (std::size_t index = 0U; index < test_case.logical_size; ++index) {
        expected.push_back(make_integer_record(test_case.seed, index));
        write_integer_record(values, index, expected.back());
    }
    expect_integer_records(values, expected);

    // Compare after every integer kernel so later updates cannot mask an
    // earlier copy, scale, or AXPY indexing error.
    for (auto& value : expected) {
        value.target_value = value.source_value;
    }
    using copy_access = fieldpack::field_access<fieldpack::read<integer_source>, fieldpack::mutate<integer_target>>;
    fieldpack::for_each_chunk<ChunkExtent>(values, copy_access{}, [](auto fields) {
        const auto sources = fields.template get<integer_source>();
        auto targets = fields.template get<integer_target>();
        for (std::size_t lane = 0U; lane < fields.size(); ++lane) {
            targets[lane] = sources[lane];
        }
    });
    expect_integer_records(values, expected);

    for (auto& value : expected) {
        value.source_value *= 3;
    }
    using scale_access = fieldpack::field_access<fieldpack::mutate<integer_source>>;
    fieldpack::for_each_chunk<ChunkExtent>(values, scale_access{}, [](auto fields) {
        auto sources = fields.template get<integer_source>();
        for (std::size_t lane = 0U; lane < fields.size(); ++lane) {
            sources[lane] *= 3;
        }
    });
    expect_integer_records(values, expected);

    for (auto& value : expected) {
        value.target_value += 2 * static_cast<std::int64_t>(value.source_value);
    }
    using axpy_access = fieldpack::field_access<fieldpack::read<integer_source>, fieldpack::mutate<integer_target>>;
    fieldpack::for_each_chunk<ChunkExtent>(values, axpy_access{}, [](auto fields) {
        const auto sources = fields.template get<integer_source>();
        auto targets = fields.template get<integer_target>();
        for (std::size_t lane = 0U; lane < fields.size(); ++lane) {
            targets[lane] += 2 * static_cast<std::int64_t>(sources[lane]);
        }
    });
    expect_integer_records(values, expected);

    const auto& observed = values;
    boost::ut::expect(execute_integer_checksum<ChunkExtent>(observed) == reference_integer_checksum(expected));
}

template<std::size_t ChunkExtent, class Layout> void check_resize_and_execute_sequence()
{
    constexpr std::array<std::size_t, 19> sizes{
        0U, 1U, 7U, 8U, 9U, 63U, 64U, 65U, 127U, 128U, 129U, 17U, 3U, 0U, 130U, 64U, 1U, 0U, 9U,
    };

    fieldpack::table<floating_schema, Layout> values;
    std::vector<floating_record> expected;
    std::size_t step = 0U;
    for (const auto new_size : sizes) {
        const auto old_size = expected.size();
        values.resize(new_size);
        expected.resize(new_size);

        for (std::size_t index = old_size; index < new_size; ++index) {
            expected[index] = make_floating_record(0xA0761D6478BD642FULL ^ step, index);
            write_floating_record(values, index, expected[index]);
        }
        expect_floating_records(values, expected);

        reference_axpy(expected, 0.25F);
        execute_axpy<ChunkExtent>(values, 0.25F);
        reference_drift(expected, 0.125);
        execute_drift<ChunkExtent>(values, 0.125);
        reference_polynomial(expected);
        execute_polynomial<ChunkExtent>(values);
        expect_floating_records(values, expected);
        expect_close(execute_checksum<ChunkExtent>(static_cast<const decltype(values)&>(values)),
                     reference_checksum(expected));
        ++step;
    }
}

[[nodiscard]] auto pseudo_random_sizes() -> std::vector<std::size_t>
{
    constexpr std::size_t requested_count = 48U;
    constexpr std::size_t largest_size = 4'096U;
    deterministic_generator generator(0xC0FFEE);
    std::vector<std::size_t> result;
    result.reserve(requested_count);
    while (result.size() < requested_count) {
        const auto candidate = static_cast<std::size_t>(generator.next()) % (largest_size + 1U);
        if (std::ranges::find(result, candidate) == result.end()) {
            result.push_back(candidate);
        }
    }
    return result;
}

template<std::size_t TileExtent, class Schema> struct generated_schema_tile;

template<std::size_t TileExtent, class First, class... Rest>
struct generated_schema_tile<TileExtent, fieldpack::schema<First, Rest...>> {
    using first_tag = fieldpack::detail::field_traits<First>::tag;
    using type = fieldpack::detail::tile_storage<TileExtent, First, Rest...>;
};

template<std::size_t TileExtent, class Schema>
using generated_schema_tile_t = generated_schema_tile<TileExtent, Schema>::type;

template<std::size_t TileExtent, class Schema>
using generated_schema_first_tag_t = generated_schema_tile<TileExtent, Schema>::first_tag;

template<class Schema, std::size_t TileExtent> void check_generated_schema_backend_control_flow()
{
    using tile_type = generated_schema_tile_t<TileExtent, Schema>;
    using allocator_type = fieldpack::detail::aligned_allocator<tile_type>;
    using storage_type = fieldpack::detail::aosoa_storage<Schema, TileExtent>;
    using first_tag = generated_schema_first_tag_t<TileExtent, Schema>;

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

void check_main_layouts(generated_case test_case)
{
    check_floating_pipeline<chunk_extent, fieldpack::soa>(test_case);
    check_floating_pipeline<chunk_extent, fieldpack::aosoa<tile_extent>>(test_case);
    check_integer_pipeline<chunk_extent, fieldpack::soa>(test_case);
    check_integer_pipeline<chunk_extent, fieldpack::aosoa<tile_extent>>(test_case);
}

} // namespace

int main() // NOLINT(bugprone-exception-escape) -- Boost.UT owns top-level test exception handling
{
    using namespace boost::ut;

    "SoA and AoSoA kernels match independent references across every boundary size"_test = [] {
        for (std::size_t size = 0U; size <= (2U * tile_extent) + chunk_extent; ++size) {
            check_main_layouts({.logical_size = size, .seed = 0xC0FFEE ^ size});
        }
    };

    "SoA and AoSoA kernels match independent references for deterministic generated data"_test = [] {
        const auto sizes = pseudo_random_sizes();
        for (std::size_t sample = 0U; sample < sizes.size(); ++sample) {
            check_main_layouts({.logical_size = sizes[sample], .seed = 0xC0FFEE42 ^ sample});
        }
    };

    "the degenerate one-lane AoSoA layout matches scalar and SoA results"_test = [] {
        constexpr std::array<std::size_t, 9> sizes{0U, 1U, 2U, 3U, 7U, 8U, 9U, 16U, 17U};
        for (const auto size : sizes) {
            const generated_case floating_case{.logical_size = size, .seed = 0xC0FFEE2 ^ size};
            const generated_case integer_case{.logical_size = size, .seed = 0xC0FFEE11 ^ size};
            check_floating_pipeline<1U, fieldpack::soa>(floating_case);
            check_floating_pipeline<1U, fieldpack::aosoa<1U>>(floating_case);
            check_integer_pipeline<1U, fieldpack::soa>(integer_case);
            check_integer_pipeline<1U, fieldpack::aosoa<1U>>(integer_case);
        }
    };

    "kernels remain equivalent through repeated cross-boundary resize sequences"_test = [] {
        check_resize_and_execute_sequence<chunk_extent, fieldpack::soa>();
        check_resize_and_execute_sequence<chunk_extent, fieldpack::aosoa<tile_extent>>();
        check_resize_and_execute_sequence<1U, fieldpack::aosoa<1U>>();
    };

    "generated integration schemas cover their template-specific backend control flow"_test = [] {
        check_generated_schema_backend_control_flow<floating_schema, tile_extent>();
        check_generated_schema_backend_control_flow<integer_schema, tile_extent>();
        check_generated_schema_backend_control_flow<floating_schema, 1U>();
        check_generated_schema_backend_control_flow<integer_schema, 1U>();
    };
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
