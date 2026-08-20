#include "benchmark_registration.hpp"
#include "benchmark_support.hpp"

#include <benchmark/benchmark.h>
#include <cstddef>
#include <fieldpack/table.hpp>

namespace {

inline constexpr double scale = -0.375;
inline constexpr fieldpack_benchmark::logical_bytes_per_item field_subset_bytes{3 * sizeof(double)};

struct fieldpack_field_subset_runner {
    template<class Layout> void operator()(benchmark::State& state) const
    {
        const auto logical_size = static_cast<std::size_t>(state.range(0));
        fieldpack::table<fieldpack_benchmark::field_subset_schema, Layout> values(logical_size);
        fieldpack_benchmark::initialize(values);

        for (auto iteration : state) {
            static_cast<void>(iteration);
            fieldpack_benchmark::field_subset(values, scale);
            benchmark::ClobberMemory();
        }

        fieldpack_benchmark::report_work(state, logical_size, field_subset_bytes);
    }
};

struct raw_field_subset_runner {
    void operator()(benchmark::State& state) const
    {
        const auto logical_size = static_cast<std::size_t>(state.range(0));
        auto values = fieldpack_benchmark::make_field_subset_arrays(logical_size);

        for (auto iteration : state) {
            static_cast<void>(iteration);
            fieldpack_benchmark::field_subset(values, scale);
            benchmark::ClobberMemory();
        }

        fieldpack_benchmark::report_work(state, logical_size, field_subset_bytes);
    }
};

// Google Benchmark discovers cases through its namespace-scope registry; this
// initialization may allocate while constructing stable benchmark names.
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
[[maybe_unused]] const bool benchmarks_registered = fieldpack_benchmark::register_benchmark_matrix(
    "field_subset", fieldpack_field_subset_runner{}, raw_field_subset_runner{});

} // namespace
