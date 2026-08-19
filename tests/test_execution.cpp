#include <array>
#include <boost/ut.hpp>
#include <concepts>
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
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

// Traversal tests deliberately use unchecked scalar and span access only with
// indices bounded by the owning table or callback-provided chunk size.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

namespace {

inline constexpr std::size_t tile_extent = 64U;

struct x {};
struct y {};
struct velocity {};
struct id {};
struct untouched {};
struct unknown {};

using traversal_schema =
    fieldpack::schema<fieldpack::field<untouched, std::int64_t>, fieldpack::field<id, std::uint32_t>,
                      fieldpack::field<y, double>, fieldpack::field<velocity, float>, fieldpack::field<x, float>>;

// Access order intentionally differs from schema order.
using traversal_access =
    fieldpack::field_access<fieldpack::mutate<y>, fieldpack::read<id>, fieldpack::mutate<x>, fieldpack::read<velocity>>;
using read_only_access = fieldpack::field_access<fieldpack::read<x>, fieldpack::read<id>>;
using mutation_access = fieldpack::field_access<fieldpack::mutate<x>>;
using unknown_access = fieldpack::field_access<fieldpack::read<unknown>>;

template<class Layout> using traversal_table = fieldpack::table<traversal_schema, Layout>;

template<class... Access>
concept formable_access = requires { typename fieldpack::field_access<Access...>; };

template<std::size_t ChunkExtent, class Table, class Access, class Function>
concept chunk_traversable = requires(Table& values, Access access, Function function) {
    { fieldpack::for_each_chunk<ChunkExtent>(values, access, function) } -> std::same_as<void>;
};

template<class Bundle, class Tag>
concept bundle_contains = requires(Bundle bundle) { bundle.template get<Tag>(); };

struct generic_read_callback {
    template<class Fields> constexpr void operator()(Fields fields) const noexcept
    {
        static_cast<void>(fields.template get<x>());
        static_cast<void>(fields.template get<id>());
    }
};

struct generic_mutation_callback {
    template<class Fields> constexpr void operator()(Fields fields) const noexcept
    {
        static_cast<void>(fields.template get<x>());
    }
};

struct potentially_throwing_callback {
    template<class Fields> void operator()(Fields /*unused*/) const {}
};

static_assert(formable_access<fieldpack::read<x>>);
static_assert(formable_access<fieldpack::mutate<x>>);
static_assert(formable_access<fieldpack::read<unknown>>);
static_assert(!formable_access<>);
static_assert(!formable_access<fieldpack::read<x>, fieldpack::read<x>>);
static_assert(!formable_access<fieldpack::read<x>, fieldpack::mutate<x>>);

using soa_table = traversal_table<fieldpack::soa>;
using aosoa_table = traversal_table<fieldpack::aosoa<tile_extent>>;

static_assert(chunk_traversable<8U, soa_table, read_only_access, generic_read_callback>);
static_assert(chunk_traversable<8U, const soa_table, read_only_access, generic_read_callback>);
static_assert(chunk_traversable<8U, aosoa_table, mutation_access, generic_mutation_callback>);
static_assert(chunk_traversable<8U, const aosoa_table, read_only_access, generic_read_callback>);
static_assert(!chunk_traversable<8U, const soa_table, mutation_access, generic_mutation_callback>);
static_assert(!chunk_traversable<8U, const aosoa_table, mutation_access, generic_mutation_callback>);
static_assert(!chunk_traversable<8U, soa_table, unknown_access, generic_read_callback>);
static_assert(!chunk_traversable<8U, aosoa_table, unknown_access, generic_read_callback>);
static_assert(!chunk_traversable<0U, soa_table, read_only_access, generic_read_callback>);
static_assert(!chunk_traversable<0U, aosoa_table, read_only_access, generic_read_callback>);
static_assert(!chunk_traversable<3U, aosoa_table, read_only_access, generic_read_callback>);
static_assert(!chunk_traversable<5U, aosoa_table, read_only_access, generic_read_callback>);
static_assert(chunk_traversable<1U, aosoa_table, read_only_access, generic_read_callback>);
static_assert(chunk_traversable<4U, aosoa_table, read_only_access, generic_read_callback>);
static_assert(chunk_traversable<8U, aosoa_table, read_only_access, generic_read_callback>);

static_assert(noexcept(fieldpack::for_each_chunk<8U>(std::declval<soa_table&>(), read_only_access{},
                                                     generic_read_callback{})));
static_assert(noexcept(fieldpack::for_each_chunk<8U>(std::declval<aosoa_table&>(), read_only_access{},
                                                     generic_read_callback{})));
static_assert(!noexcept(fieldpack::for_each_chunk<8U>(std::declval<soa_table&>(), read_only_access{},
                                                      potentially_throwing_callback{})));
static_assert(!noexcept(fieldpack::for_each_chunk<8U>(std::declval<aosoa_table&>(), read_only_access{},
                                                      potentially_throwing_callback{})));

template<class Table> void initialize_traversal_table(Table& values)
{
    for (std::size_t index = 0; index < values.size(); ++index) {
        auto row = values[index];
        row.template get<x>() = static_cast<float>(index) + 0.25F;
        row.template get<y>() = static_cast<double>(index) + 0.5;
        row.template get<velocity>() = static_cast<float>((index % 7U) + 1U);
        row.template get<id>() = static_cast<std::uint32_t>(index);
        row.template get<untouched>() = -static_cast<std::int64_t>(index) - 17;
    }
}

template<std::size_t ChunkExtent, class Layout> void check_one_traversal_size(std::size_t logical_size)
{
    traversal_table<Layout> values(logical_size);
    initialize_traversal_table(values);

    std::vector<std::size_t> visits(logical_size, 0U);
    std::vector<std::size_t> order;
    order.reserve(logical_size);
    std::size_t full_chunk_count = 0U;
    std::size_t tail_count = 0U;
    std::size_t visited_count = 0U;

    fieldpack::for_each_chunk<ChunkExtent>(values, traversal_access{}, [&](auto fields) {
        auto y_values = fields.template get<y>();
        auto ids = fields.template get<id>();
        auto x_values = fields.template get<x>();
        auto velocities = fields.template get<velocity>();

        using y_span = std::remove_cvref_t<decltype(y_values)>;
        using id_span = std::remove_cvref_t<decltype(ids)>;
        using x_span = std::remove_cvref_t<decltype(x_values)>;
        using velocity_span = std::remove_cvref_t<decltype(velocities)>;
        constexpr auto extent = x_span::extent;

        static_assert(std::same_as<typename y_span::element_type, double>);
        static_assert(std::same_as<typename id_span::element_type, const std::uint32_t>);
        static_assert(std::same_as<typename x_span::element_type, float>);
        static_assert(std::same_as<typename velocity_span::element_type, const float>);
        static_assert(extent == ChunkExtent || extent == std::dynamic_extent);
        static_assert(y_span::extent == extent);
        static_assert(id_span::extent == extent);
        static_assert(velocity_span::extent == extent);
        static_assert(bundle_contains<decltype(fields), x>);
        static_assert(bundle_contains<decltype(fields), id>);
        static_assert(!bundle_contains<decltype(fields), untouched>);

        boost::ut::expect(fields.size() == x_values.size());
        boost::ut::expect(y_values.size() == x_values.size());
        boost::ut::expect(ids.size() == x_values.size());
        boost::ut::expect(velocities.size() == x_values.size());

        if constexpr (extent == ChunkExtent) {
            ++full_chunk_count;
            boost::ut::expect(fields.size() == ChunkExtent);

            if constexpr (ChunkExtent > 1U) {
                const auto first = static_cast<std::size_t>(ids.front());
                const auto last = static_cast<std::size_t>(ids.back());
                boost::ut::expect(first / tile_extent == last / tile_extent);
            }
        }
        else {
            ++tail_count;
            boost::ut::expect(!fields.empty());
            boost::ut::expect(fields.size() < ChunkExtent);
        }

        for (std::size_t lane = 0; lane < fields.size(); ++lane) {
            const auto logical_index = static_cast<std::size_t>(ids[lane]);
            boost::ut::expect(logical_index < logical_size);
            boost::ut::expect(logical_index == visited_count);
            ++visits[logical_index];
            order.push_back(logical_index);
            ++visited_count;

            x_values[lane] += 2.0F * velocities[lane];
            y_values[lane] -= static_cast<double>(velocities[lane]);
        }
    });

    boost::ut::expect(full_chunk_count == logical_size / ChunkExtent);
    boost::ut::expect(tail_count == static_cast<std::size_t>(logical_size % ChunkExtent != 0U));
    boost::ut::expect(visited_count == logical_size);
    boost::ut::expect(order.size() == logical_size);

    for (std::size_t index = 0; index < logical_size; ++index) {
        boost::ut::expect(visits[index] == 1U);
        boost::ut::expect(order[index] == index);

        const auto row = values[index];
        const auto speed = static_cast<float>((index % 7U) + 1U);
        boost::ut::expect(row.template get<x>() == static_cast<float>(index) + 0.25F + (2.0F * speed));
        boost::ut::expect(row.template get<y>() == static_cast<double>(index) + 0.5 - static_cast<double>(speed));
        boost::ut::expect(row.template get<velocity>() == speed);
        boost::ut::expect(row.template get<id>() == static_cast<std::uint32_t>(index));
        boost::ut::expect(row.template get<untouched>() == -static_cast<std::int64_t>(index) - 17);
    }
}

template<std::size_t ChunkExtent, class Layout> void check_explicit_boundaries()
{
    constexpr std::array<std::size_t, 11> sizes{
        0U,
        1U,
        ChunkExtent - 1U,
        ChunkExtent,
        ChunkExtent + 1U,
        tile_extent - 1U,
        tile_extent,
        tile_extent + 1U,
        (2U * tile_extent) - 1U,
        2U * tile_extent,
        (2U * tile_extent) + 1U,
    };

    for (const auto size : sizes) {
        check_one_traversal_size<ChunkExtent, Layout>(size);
    }
}

template<std::size_t ChunkExtent, class Layout> void check_exhaustive_sizes()
{
    for (std::size_t size = 0U; size <= (2U * tile_extent) + ChunkExtent; ++size) {
        check_one_traversal_size<ChunkExtent, Layout>(size);
    }
}

template<std::size_t ChunkExtent> void check_layout_traversal_contract()
{
    check_explicit_boundaries<ChunkExtent, fieldpack::soa>();
    check_explicit_boundaries<ChunkExtent, fieldpack::aosoa<tile_extent>>();
    check_exhaustive_sizes<ChunkExtent, fieldpack::soa>();
    check_exhaustive_sizes<ChunkExtent, fieldpack::aosoa<tile_extent>>();
}

template<std::size_t ChunkExtent, class Layout> void check_read_only_const_table(std::size_t logical_size)
{
    traversal_table<Layout> values(logical_size);
    initialize_traversal_table(values);
    const auto& observed = values;

    std::uint64_t checksum = 0U;
    fieldpack::for_each_chunk<ChunkExtent>(observed, read_only_access{}, [&](auto fields) {
        auto x_values = fields.template get<x>();
        auto ids = fields.template get<id>();
        using x_span = std::remove_cvref_t<decltype(x_values)>;
        using id_span = std::remove_cvref_t<decltype(ids)>;
        static_assert(std::same_as<typename x_span::element_type, const float>);
        static_assert(std::same_as<typename id_span::element_type, const std::uint32_t>);

        for (std::size_t lane = 0; lane < fields.size(); ++lane) {
            checksum += ids[lane] + static_cast<std::uint64_t>(x_values[lane]);
        }
    });

    std::uint64_t expected = 0U;
    for (std::size_t index = 0; index < logical_size; ++index) {
        expected += static_cast<std::uint64_t>(index) + static_cast<std::uint64_t>(static_cast<float>(index) + 0.25F);
    }
    boost::ut::expect(checksum == expected);
}

struct drift_x {};
struct drift_y {};
struct drift_vx {};
struct drift_vy {};
struct drift_unused {};

using drift_schema = fieldpack::schema<fieldpack::field<drift_unused, std::uint32_t>,
                                       fieldpack::field<drift_vy, double>, fieldpack::field<drift_x, double>,
                                       fieldpack::field<drift_vx, double>, fieldpack::field<drift_y, double>>;
using drift_access = fieldpack::field_access<fieldpack::read<drift_vx>, fieldpack::mutate<drift_y>,
                                             fieldpack::read<drift_vy>, fieldpack::mutate<drift_x>>;

struct drift_result {
    double x;
    double y;
    std::uint32_t unused;

    auto operator==(const drift_result&) const -> bool = default;
};

template<class Layout, std::size_t ChunkExtent> auto run_drift(std::size_t logical_size) -> std::vector<drift_result>
{
    fieldpack::table<drift_schema, Layout> values(logical_size);
    for (std::size_t index = 0; index < logical_size; ++index) {
        auto row = values[index];
        row.template get<drift_x>() = static_cast<double>(index) * 0.5;
        row.template get<drift_y>() = -static_cast<double>(index) * 0.25;
        row.template get<drift_vx>() = static_cast<double>((index % 5U) + 1U);
        row.template get<drift_vy>() = -static_cast<double>((index % 3U) + 1U);
        row.template get<drift_unused>() = static_cast<std::uint32_t>(10'000U + index);
    }

    constexpr double time_step = 0.125;
    fieldpack::for_each_chunk<ChunkExtent>(values, drift_access{}, [](auto fields) {
        auto vxs = fields.template get<drift_vx>();
        auto y_values = fields.template get<drift_y>();
        auto vys = fields.template get<drift_vy>();
        auto x_values = fields.template get<drift_x>();
        for (std::size_t lane = 0; lane < fields.size(); ++lane) {
            x_values[lane] += time_step * vxs[lane];
            y_values[lane] += time_step * vys[lane];
        }
    });

    std::vector<drift_result> result;
    result.reserve(logical_size);
    for (std::size_t index = 0; index < logical_size; ++index) {
        const auto row = values[index];
        result.push_back({
            .x = row.template get<drift_x>(),
            .y = row.template get<drift_y>(),
            .unused = row.template get<drift_unused>(),
        });
    }
    return result;
}

auto reference_drift(std::size_t logical_size) -> std::vector<drift_result>
{
    std::vector<drift_result> result;
    result.reserve(logical_size);
    constexpr double time_step = 0.125;
    for (std::size_t index = 0; index < logical_size; ++index) {
        const auto initial_x = static_cast<double>(index) * 0.5;
        const auto initial_y = -static_cast<double>(index) * 0.25;
        const auto velocity_x = static_cast<double>((index % 5U) + 1U);
        const auto velocity_y = -static_cast<double>((index % 3U) + 1U);
        result.push_back({
            .x = initial_x + (time_step * velocity_x),
            .y = initial_y + (time_step * velocity_y),
            .unused = static_cast<std::uint32_t>(10'000U + index),
        });
    }
    return result;
}

struct c0 {};
struct c1 {};
struct c2 {};
struct c3 {};
struct polynomial_x {};
struct polynomial_y {};

using polynomial_schema = fieldpack::schema<fieldpack::field<c2, double>, fieldpack::field<polynomial_y, double>,
                                            fieldpack::field<c0, double>, fieldpack::field<polynomial_x, double>,
                                            fieldpack::field<c3, double>, fieldpack::field<c1, double>>;
using polynomial_access =
    fieldpack::field_access<fieldpack::read<c0>, fieldpack::read<c1>, fieldpack::read<c2>, fieldpack::read<c3>,
                            fieldpack::read<polynomial_x>, fieldpack::mutate<polynomial_y>>;

template<class Layout, std::size_t ChunkExtent> auto run_polynomial(std::size_t logical_size) -> std::vector<double>
{
    fieldpack::table<polynomial_schema, Layout> values(logical_size);
    for (std::size_t index = 0; index < logical_size; ++index) {
        auto row = values[index];
        row.template get<c0>() = static_cast<double>(index % 11U) - 5.0;
        row.template get<c1>() = 0.25 * static_cast<double>((index % 7U) + 1U);
        row.template get<c2>() = -0.5 * static_cast<double>((index % 5U) + 1U);
        row.template get<c3>() = 0.125 * static_cast<double>((index % 3U) + 1U);
        row.template get<polynomial_x>() = 0.5 * static_cast<double>(index % 9U);
        row.template get<polynomial_y>() = -999.0;
    }

    fieldpack::for_each_chunk<ChunkExtent>(values, polynomial_access{}, [](auto fields) {
        auto c0s = fields.template get<c0>();
        auto c1s = fields.template get<c1>();
        auto c2s = fields.template get<c2>();
        auto c3s = fields.template get<c3>();
        auto x_values = fields.template get<polynomial_x>();
        auto y_values = fields.template get<polynomial_y>();
        for (std::size_t lane = 0; lane < fields.size(); ++lane) {
            y_values[lane] =
                (((((c3s[lane] * x_values[lane]) + c2s[lane]) * x_values[lane]) + c1s[lane]) * x_values[lane]) +
                c0s[lane];
        }
    });

    std::vector<double> result;
    result.reserve(logical_size);
    for (std::size_t index = 0; index < logical_size; ++index) {
        result.push_back(values[index].template get<polynomial_y>());
    }
    return result;
}

auto reference_polynomial(std::size_t logical_size) -> std::vector<double>
{
    std::vector<double> result;
    result.reserve(logical_size);
    for (std::size_t index = 0; index < logical_size; ++index) {
        const auto coefficient0 = static_cast<double>(index % 11U) - 5.0;
        const auto coefficient1 = 0.25 * static_cast<double>((index % 7U) + 1U);
        const auto coefficient2 = -0.5 * static_cast<double>((index % 5U) + 1U);
        const auto coefficient3 = 0.125 * static_cast<double>((index % 3U) + 1U);
        const auto input = 0.5 * static_cast<double>(index % 9U);
        result.push_back((((((coefficient3 * input) + coefficient2) * input) + coefficient1) * input) + coefficient0);
    }
    return result;
}

template<std::size_t ChunkExtent> void check_numerical_layout_equivalence()
{
    for (std::size_t size = 0U; size <= (2U * tile_extent) + ChunkExtent; ++size) {
        const auto expected_drift = reference_drift(size);
        boost::ut::expect((run_drift<fieldpack::soa, ChunkExtent>(size) == expected_drift));
        boost::ut::expect((run_drift<fieldpack::aosoa<tile_extent>, ChunkExtent>(size) == expected_drift));

        const auto expected_polynomial = reference_polynomial(size);
        boost::ut::expect((run_polynomial<fieldpack::soa, ChunkExtent>(size) == expected_polynomial));
        boost::ut::expect((run_polynomial<fieldpack::aosoa<tile_extent>, ChunkExtent>(size) == expected_polynomial));
    }
}

template<class Schema> struct schema_tile;

template<class First, class... Rest> struct schema_tile<fieldpack::schema<First, Rest...>> {
    using first_tag = fieldpack::detail::field_traits<First>::tag;
    using type = fieldpack::detail::tile_storage<tile_extent, First, Rest...>;
};

template<class Schema> using schema_tile_t = schema_tile<Schema>::type;

template<class Schema> using schema_first_tag_t = schema_tile<Schema>::first_tag;

template<class Schema> void check_execution_schema_backend_control_flow()
{
    using tile_type = schema_tile_t<Schema>;
    using allocator_type = fieldpack::detail::aligned_allocator<tile_type>;
    using storage_type = fieldpack::detail::aosoa_storage<Schema, tile_extent>;
    using first_tag = schema_first_tag_t<Schema>;

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

    storage_type values((2U * tile_extent) + 1U);
    boost::ut::expect(!values.empty());
    boost::ut::expect(values.physical_tile_count() == 3U);
    boost::ut::expect(values.contiguous_count(0U) == tile_extent);
    boost::ut::expect(values.contiguous_count(2U * tile_extent) == 1U);
    boost::ut::expect(values.contiguous_count(values.size()) == 0U);

    auto empty_span = values.template contiguous_span<first_tag>(values.size(), 0U);
    boost::ut::expect(empty_span.empty());
    const auto& observed = values;
    auto empty_const_span = observed.template contiguous_span<first_tag>(observed.size(), 0U);
    boost::ut::expect(empty_const_span.empty());

    values.resize(values.size());
    values.resize(2U * tile_extent);
    values.resize(tile_extent + 1U);
    values.resize(tile_extent + 2U);
    values.resize(2U * tile_extent);
    values.resize((2U * tile_extent) + 1U);

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
    boost::ut::expect(move_assigned.size() == (2U * tile_extent) + 1U);

    values.resize(0U);
    boost::ut::expect(values.empty());
    values.resize(tile_extent);
    boost::ut::expect(values.size() == tile_extent);

    boost::ut::expect(boost::ut::throws<std::bad_array_new_length>(
        [] { static_cast<void>(storage_type{std::numeric_limits<std::size_t>::max()}); }));
}

template<class Layout> void check_callback_category_runtime_paths()
{
    traversal_table<Layout> values(9U);
    fieldpack::for_each_chunk<8U>(values, read_only_access{}, generic_read_callback{});
    fieldpack::for_each_chunk<8U>(values, mutation_access{}, generic_mutation_callback{});
    fieldpack::for_each_chunk<8U>(values, read_only_access{}, potentially_throwing_callback{});

    const auto& observed = values;
    fieldpack::for_each_chunk<8U>(observed, read_only_access{}, generic_read_callback{});
    fieldpack::for_each_chunk<8U>(observed, read_only_access{}, potentially_throwing_callback{});

    boost::ut::expect(observed.size() == 9U);
}

struct callback_error : std::runtime_error {
    callback_error() : std::runtime_error{"intentional traversal callback failure"} {}
};

struct controlled_throwing_callback {
    std::size_t* callback_count;
    bool throw_on_call;

    template<class Fields> void operator()(Fields fields) const
    {
        ++*callback_count;
        auto x_values = fields.template get<x>();
        x_values.front() = -10.0F;
        if (throw_on_call) {
            throw callback_error{};
        }
    }
};

template<class Layout> void check_callback_exception_propagation()
{
    traversal_table<Layout> values(9U);
    initialize_traversal_table(values);

    std::size_t callback_count = 0U;
    controlled_throwing_callback callback{.callback_count = &callback_count, .throw_on_call = true};
    const auto traverse = [&] { fieldpack::for_each_chunk<4U>(values, mutation_access{}, callback); };

    boost::ut::expect(boost::ut::throws<callback_error>(traverse));
    boost::ut::expect(callback_count == 1U);
    boost::ut::expect(values[0].template get<x>() == -10.0F);
    boost::ut::expect(values[1].template get<x>() == 1.25F);

    callback.throw_on_call = false;
    fieldpack::for_each_chunk<4U>(values, mutation_access{}, callback);
    boost::ut::expect(callback_count == 4U);
    boost::ut::expect(values[8].template get<x>() == -10.0F);
}

} // namespace

int main() // NOLINT(bugprone-exception-escape) -- Boost.UT owns top-level test exception handling
{
    using namespace boost::ut;

    "chunk extent 1 obeys the complete traversal contract for both layouts"_test = [] {
        check_layout_traversal_contract<1U>();
    };

    "chunk extent 4 obeys the complete traversal contract for both layouts"_test = [] {
        check_layout_traversal_contract<4U>();
    };

    "chunk extent 8 obeys the complete traversal contract for both layouts"_test = [] {
        check_layout_traversal_contract<8U>();
    };

    "const tables support read-only traversal with const spans"_test = [] {
        check_read_only_const_table<1U, fieldpack::soa>(131U);
        check_read_only_const_table<4U, fieldpack::soa>(131U);
        check_read_only_const_table<8U, fieldpack::soa>(131U);
        check_read_only_const_table<1U, fieldpack::aosoa<tile_extent>>(131U);
        check_read_only_const_table<4U, fieldpack::aosoa<tile_extent>>(131U);
        check_read_only_const_table<8U, fieldpack::aosoa<tile_extent>>(131U);
    };

    "drift and polynomial kernels match scalar references for every swept size"_test = [] {
        check_numerical_layout_equivalence<1U>();
        check_numerical_layout_equivalence<4U>();
        check_numerical_layout_equivalence<8U>();
    };

    "callback exceptions propagate without rolling back completed mutations"_test = [] {
        check_callback_exception_propagation<fieldpack::soa>();
        check_callback_exception_propagation<fieldpack::aosoa<tile_extent>>();
    };

    "execution schemas cover production backend edge paths in this executable"_test = [] {
        check_execution_schema_backend_control_flow<traversal_schema>();
        check_execution_schema_backend_control_flow<drift_schema>();
        check_execution_schema_backend_control_flow<polynomial_schema>();
    };

    "compile-time callback categories also exercise full and tail runtime paths"_test = [] {
        check_callback_category_runtime_paths<fieldpack::soa>();
        check_callback_category_runtime_paths<fieldpack::aosoa<tile_extent>>();
    };
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
