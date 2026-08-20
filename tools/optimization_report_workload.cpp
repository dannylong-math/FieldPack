#include "benchmark_support.hpp"

#include <cstddef>
#include <fieldpack/layout.hpp>
#include <fieldpack/table.hpp>
#include <vector>

namespace {

constexpr std::size_t workload_size = 65U;

template<class Layout> auto run_representative_kernels() -> double
{
    fieldpack::table<fieldpack_benchmark::polynomial_schema, Layout> polynomial(workload_size);
    fieldpack::table<fieldpack_benchmark::drift_schema, Layout> drift(workload_size);
    fieldpack::table<fieldpack_benchmark::field_subset_schema, Layout> subset(workload_size);

    fieldpack_benchmark::initialize(polynomial);
    fieldpack_benchmark::initialize(drift);
    fieldpack_benchmark::initialize(subset);

    fieldpack_benchmark::polynomial_evaluation(polynomial);
    fieldpack_benchmark::drift(drift, 0.125);
    fieldpack_benchmark::field_subset(subset, -0.375);

    return polynomial.at(workload_size - 1U).template get<fieldpack_benchmark::tags::polynomial_output>()
           + drift.at(workload_size - 1U).template get<fieldpack_benchmark::tags::position_x>()
           + subset.at(workload_size - 1U).template get<fieldpack_benchmark::tags::hot_target>();
}

// The data-dependent exit intentionally gives optimization reports a stable
// inhibited-loop example alongside the vectorizable benchmark kernels.
[[gnu::noinline]] auto deliberately_inhibited_loop(const std::vector<double>& input) -> double
{
    double result = 0.0;
    for (const double value : input) {
        if (value < 0.0) {
            break;
        }
        result += value;
    }
    return result;
}

} // namespace

int main() // NOLINT(bugprone-exception-escape) -- a failed report workload must terminate the build target
{
    const std::vector<double> inhibited_input{1.0, 2.0, -1.0, 4.0};
    const auto checksum = run_representative_kernels<fieldpack::soa>()
                          + run_representative_kernels<fieldpack::aosoa<64U>>()
                          + deliberately_inhibited_loop(inhibited_input);
    return checksum == 0.0 ? 1 : 0;
}
