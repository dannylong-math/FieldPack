#include "benchmark_registration.hpp"
#include "benchmark_support.hpp"

#include <benchmark/benchmark.h>
#include <cstddef>
#include <fieldpack/table.hpp>

namespace {

inline constexpr fieldpack_benchmark::logical_bytes_per_item polynomial_bytes{6 * sizeof(double)};

struct fieldpack_polynomial_runner {
    template<class Layout> void operator()(benchmark::State& state) const
    {
        const auto logical_size = static_cast<std::size_t>(state.range(0));
        fieldpack::table<fieldpack_benchmark::polynomial_schema, Layout> values(logical_size);
        fieldpack_benchmark::initialize(values);

        for (auto iteration : state) {
            static_cast<void>(iteration);
            fieldpack_benchmark::polynomial_evaluation(values);
            benchmark::ClobberMemory();
        }

        fieldpack_benchmark::report_work(state, logical_size, polynomial_bytes);
    }
};

struct raw_polynomial_runner {
    void operator()(benchmark::State& state) const
    {
        const auto logical_size = static_cast<std::size_t>(state.range(0));
        auto values = fieldpack_benchmark::make_polynomial_arrays(logical_size);

        for (auto iteration : state) {
            static_cast<void>(iteration);
            fieldpack_benchmark::polynomial_evaluation(values);
            benchmark::ClobberMemory();
        }

        fieldpack_benchmark::report_work(state, logical_size, polynomial_bytes);
    }
};

// Google Benchmark discovers cases through its namespace-scope registry; this
// initialization may allocate while constructing stable benchmark names.
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
[[maybe_unused]] const bool benchmarks_registered = fieldpack_benchmark::register_benchmark_matrix(
    "polynomial", fieldpack_polynomial_runner{}, raw_polynomial_runner{});

} // namespace
