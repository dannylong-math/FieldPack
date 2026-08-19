#include "benchmark_registration.hpp"
#include "benchmark_support.hpp"

#include <benchmark/benchmark.h>
#include <cstddef>
#include <fieldpack/table.hpp>

namespace {

inline constexpr fieldpack_benchmark::logical_bytes_per_item reduction_bytes{4 * sizeof(double)};

struct fieldpack_reduction_runner {
    template<class Layout> void operator()(benchmark::State& state) const
    {
        const auto logical_size = static_cast<std::size_t>(state.range(0));
        fieldpack::table<fieldpack_benchmark::reduction_schema, Layout> values(logical_size);
        fieldpack_benchmark::initialize(values);
        const auto& observed = values;

        for (auto iteration : state) {
            static_cast<void>(iteration);
            auto checksum = fieldpack_benchmark::reduction(observed);
            benchmark::DoNotOptimize(checksum);
        }

        fieldpack_benchmark::report_work(state, logical_size, reduction_bytes);
    }
};

struct raw_reduction_runner {
    void operator()(benchmark::State& state) const
    {
        const auto logical_size = static_cast<std::size_t>(state.range(0));
        const auto values = fieldpack_benchmark::make_reduction_arrays(logical_size);

        for (auto iteration : state) {
            static_cast<void>(iteration);
            auto checksum = fieldpack_benchmark::reduction(values);
            benchmark::DoNotOptimize(checksum);
        }

        fieldpack_benchmark::report_work(state, logical_size, reduction_bytes);
    }
};

// Google Benchmark discovers cases through its namespace-scope registry; this
// initialization may allocate while constructing stable benchmark names.
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
[[maybe_unused]] const bool benchmarks_registered = fieldpack_benchmark::register_benchmark_matrix(
    "read_only_reduction", fieldpack_reduction_runner{}, raw_reduction_runner{});

} // namespace
