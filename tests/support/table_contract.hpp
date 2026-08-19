#pragma once

#include <array>
#include <boost/ut.hpp>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <fieldpack/schema.hpp>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace fieldpack_test {

struct x {};
struct y {};
struct id {};
struct count {};

using x_field = fieldpack::field<x, float>;
using y_field = fieldpack::field<y, double>;
using id_field = fieldpack::field<id, std::uint32_t>;
using count_field = fieldpack::field<count, std::int64_t>;

using mixed_schema = fieldpack::schema<x_field, y_field, id_field, count_field>;
using reordered_schema = fieldpack::schema<count_field, id_field, y_field, x_field>;

template<class Table> void expect_zero_initialized(const Table& values)
{
    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto row = values[index];
        boost::ut::expect(row.template get<x>() == 0.0F);
        boost::ut::expect(row.template get<y>() == 0.0);
        boost::ut::expect(row.template get<id>() == std::uint32_t{});
        boost::ut::expect(row.template get<count>() == std::int64_t{});
    }
}

template<class Table> void write_record(Table& values, std::size_t index, std::size_t seed)
{
    auto row = values[index];
    row.template get<x>() = static_cast<float>(seed) + 0.25F;
    row.template get<y>() = static_cast<double>(seed) + 0.5;
    row.template get<id>() = static_cast<std::uint32_t>(1'000U + seed);
    row.template get<count>() = -static_cast<std::int64_t>(seed) - 7;
}

template<class Table> void expect_record(const Table& values, std::size_t index, std::size_t seed)
{
    const auto row = values[index];
    boost::ut::expect(row.template get<x>() == static_cast<float>(seed) + 0.25F);
    boost::ut::expect(row.template get<y>() == static_cast<double>(seed) + 0.5);
    boost::ut::expect(row.template get<id>() == static_cast<std::uint32_t>(1'000U + seed));
    boost::ut::expect(row.template get<count>() == -static_cast<std::int64_t>(seed) - 7);
}

template<class Table> consteval void check_row_reference_types()
{
    using mutable_row = decltype(std::declval<Table&>()[std::size_t{}]);
    using const_row = decltype(std::declval<const Table&>()[std::size_t{}]);

    static_assert(std::same_as<decltype(std::declval<mutable_row&>().template get<x>()), float&>);
    static_assert(std::same_as<decltype(std::declval<const mutable_row&>().template get<x>()), float&>);
    static_assert(std::same_as<decltype(std::declval<const_row&>().template get<x>()), const float&>);
    static_assert(std::same_as<decltype(std::declval<const const_row&>().template get<x>()), const float&>);

    static_assert(std::same_as<decltype(std::declval<Table&>().at(std::size_t{}).template get<id>()), std::uint32_t&>);
    static_assert(std::same_as<decltype(std::declval<const Table&>().at(std::size_t{}).template get<id>()),
                               const std::uint32_t&>);
}

template<class Table> void check_construction_contract()
{
    constexpr std::array<std::size_t, 4> sizes{0, 1, 7, 4'097};

    for (const auto requested_size : sizes) {
        const Table values(requested_size);
        boost::ut::expect(values.size() == requested_size);
        boost::ut::expect(values.empty() == (requested_size == 0));
        expect_zero_initialized(values);
    }
}

template<class Table> void check_tag_access_and_proxy_contract()
{
    Table values(3);

    auto row = values[1];
    row.template get<x>() = 1.25F;
    row.template get<y>() = 2.5;
    row.template get<id>() = 31U;
    row.template get<count>() = -9;

    row.template get<x>() = 4.5F;
    boost::ut::expect(row.template get<y>() == 2.5);
    boost::ut::expect(row.template get<id>() == 31U);
    boost::ut::expect(row.template get<count>() == -9);

    row.template get<y>() = 8.75;
    boost::ut::expect(row.template get<x>() == 4.5F);
    boost::ut::expect(row.template get<id>() == 31U);
    boost::ut::expect(row.template get<count>() == -9);

    row.template get<id>() = 73U;
    boost::ut::expect(row.template get<x>() == 4.5F);
    boost::ut::expect(row.template get<y>() == 8.75);
    boost::ut::expect(row.template get<count>() == -9);

    row.template get<count>() = -41;
    boost::ut::expect(row.template get<x>() == 4.5F);
    boost::ut::expect(row.template get<y>() == 8.75);
    boost::ut::expect(row.template get<id>() == 73U);

    // A proxy aliases its source table, and constness of a mutable proxy does
    // not remove the source table's write permission.
    const auto mutable_proxy = values[1];
    mutable_proxy.template get<x>() = 19.0F;
    boost::ut::expect(values[1].template get<x>() == 19.0F);

    values[1].template get<id>() = 99U;
    boost::ut::expect(row.template get<id>() == 99U);

    const Table& observed = values;
    boost::ut::expect(observed[1].template get<x>() == 19.0F);
    boost::ut::expect(observed[1].template get<y>() == 8.75);
    boost::ut::expect(observed[1].template get<id>() == 99U);
    boost::ut::expect(observed[1].template get<count>() == -41);
}

template<class Table> void check_bounds_contract()
{
    Table values(4);
    write_record(values, 0, 10);
    write_record(values, 3, 30);

    expect_record(values, 0, 10);
    expect_record(values, 3, 30);
    expect_record(static_cast<const Table&>(values), 3, 30);

    boost::ut::expect(boost::ut::throws<std::out_of_range>([&] { static_cast<void>(values.at(values.size())); }));
    boost::ut::expect(boost::ut::throws<std::out_of_range>([&] { static_cast<void>(values.at(values.size() + 9U)); }));

    const Table& observed = values;
    boost::ut::expect(boost::ut::throws<std::out_of_range>([&] { static_cast<void>(observed.at(observed.size())); }));
    boost::ut::expect(
        boost::ut::throws<std::out_of_range>([&] { static_cast<void>(observed.at(observed.size() + 9U)); }));
}

template<class Table> void check_copy_and_move_contract()
{
    Table original(5);
    for (std::size_t index = 0; index < original.size(); ++index) {
        write_record(original, index, index + 10U);
    }

    Table copied(original);
    for (std::size_t index = 0; index < copied.size(); ++index) {
        expect_record(copied, index, index + 10U);
    }
    copied[0].template get<x>() = -1.0F;
    copied[1].template get<id>() = 7U;
    expect_record(original, 0, 10U);
    expect_record(original, 1, 11U);

    Table copy_assigned(2);
    copy_assigned = original;
    for (std::size_t index = 0; index < copy_assigned.size(); ++index) {
        expect_record(copy_assigned, index, index + 10U);
    }
    copy_assigned[2].template get<y>() = -3.0;
    expect_record(original, 2, 12U);

    Table move_source(4);
    for (std::size_t index = 0; index < move_source.size(); ++index) {
        write_record(move_source, index, index + 20U);
    }
    Table moved(std::move(move_source));
    boost::ut::expect(moved.size() == 4U);
    for (std::size_t index = 0; index < moved.size(); ++index) {
        expect_record(moved, index, index + 20U);
    }

    Table move_assignment_source(6);
    for (std::size_t index = 0; index < move_assignment_source.size(); ++index) {
        write_record(move_assignment_source, index, index + 30U);
    }
    Table move_assigned(1);
    move_assigned = std::move(move_assignment_source);
    boost::ut::expect(move_assigned.size() == 6U);
    for (std::size_t index = 0; index < move_assigned.size(); ++index) {
        expect_record(move_assigned, index, index + 30U);
    }
}

template<class Table> void check_resize_contract()
{
    Table values(3);
    for (std::size_t index = 0; index < values.size(); ++index) {
        write_record(values, index, index + 40U);
    }

    values.resize(8);
    boost::ut::expect(values.size() == 8U);
    boost::ut::expect(!values.empty());
    for (std::size_t index = 0; index < 3; ++index) {
        expect_record(values, index, index + 40U);
    }
    for (std::size_t index = 3; index < values.size(); ++index) {
        const auto row = static_cast<const Table&>(values)[index];
        boost::ut::expect(row.template get<x>() == 0.0F);
        boost::ut::expect(row.template get<y>() == 0.0);
        boost::ut::expect(row.template get<id>() == std::uint32_t{});
        boost::ut::expect(row.template get<count>() == std::int64_t{});
    }

    write_record(values, 5, 95U);
    values.resize(values.size());
    boost::ut::expect(values.size() == 8U);
    expect_record(values, 5, 95U);

    values.resize(2);
    boost::ut::expect(values.size() == 2U);
    for (std::size_t index = 0; index < values.size(); ++index) {
        expect_record(values, index, index + 40U);
    }

    values.resize(0);
    boost::ut::expect(values.size() == 0U);
    boost::ut::expect(values.empty());

    values.resize(3);
    boost::ut::expect(values.size() == 3U);
    expect_zero_initialized(values);

    Table stale_value_check(6);
    for (std::size_t index = 0; index < stale_value_check.size(); ++index) {
        write_record(stale_value_check, index, index + 70U);
    }
    stale_value_check.resize(2);
    stale_value_check.resize(6);

    expect_record(stale_value_check, 0, 70U);
    expect_record(stale_value_check, 1, 71U);
    for (std::size_t index = 2; index < stale_value_check.size(); ++index) {
        const auto row = static_cast<const Table&>(stale_value_check)[index];
        boost::ut::expect(row.template get<x>() == 0.0F);
        boost::ut::expect(row.template get<y>() == 0.0);
        boost::ut::expect(row.template get<id>() == std::uint32_t{});
        boost::ut::expect(row.template get<count>() == std::int64_t{});
    }
}

template<class Table> void check_scalar_table_contract()
{
    check_row_reference_types<Table>();
    check_construction_contract<Table>();
    check_tag_access_and_proxy_contract<Table>();
    check_bounds_contract<Table>();
    check_copy_and_move_contract<Table>();
    check_resize_contract<Table>();
}

} // namespace fieldpack_test
