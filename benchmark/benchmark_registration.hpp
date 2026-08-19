#pragma once // NOLINT(portability-avoid-pragma-once) -- project-wide header convention

#include "benchmark_support.hpp"

#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdint>
#include <fieldpack/layout.hpp>
#include <string>
#include <string_view>
#include <utility>

/**
 * @file benchmark_registration.hpp
 * @brief Google Benchmark registration and throughput-accounting helpers.
 */

namespace fieldpack_benchmark {

/** @brief Strong logical-byte count used to prevent swapped arguments. */
struct logical_bytes_per_item {
    std::int64_t value;
};

/** @brief Stable components used to name one registered benchmark family. */
struct benchmark_identity {
    std::string_view kernel;
    std::string_view implementation;
    std::string_view layout;
};

/**
 * @brief Report logical records and field bytes processed by a timed kernel.
 *
 * The byte count represents explicit logical field reads and writes, not
 * cache-line transfers or measured hardware traffic.
 */
inline void report_work(benchmark::State& state, std::size_t logical_size, logical_bytes_per_item bytes)
{
    const auto items_per_iteration = static_cast<std::int64_t>(logical_size);
    state.SetItemsProcessed(state.iterations() * items_per_iteration);
    state.SetBytesProcessed(state.iterations() * items_per_iteration * bytes.value);
}

namespace detail {

/** @brief Join stable benchmark identity components with slash separators. */
[[nodiscard]] inline auto benchmark_name(benchmark_identity identity) -> std::string
{
    std::string result;
    result.reserve(identity.kernel.size() + identity.implementation.size() + identity.layout.size() + 2U);
    result.append(identity.kernel);
    result.push_back('/');
    result.append(identity.implementation);
    result.push_back('/');
    result.append(identity.layout);
    return result;
}

/** @brief Register one callable at every approved problem size. */
template<class Callable> void register_case(benchmark_identity identity, Callable&& callable)
{
    const auto name = benchmark_name(identity);
    auto* registration = benchmark::RegisterBenchmark(name, std::forward<Callable>(callable));
    for (const auto size : problem_sizes()) {
        registration->Arg(static_cast<std::int64_t>(size));
    }
}

/** @brief Register one FieldPack layout specialization for a kernel runner. */
template<class Layout, class Runner> void register_fieldpack_layout(std::string_view kernel, Runner runner)
{
    register_case({.kernel = kernel, .implementation = "fieldpack", .layout = layout_label<Layout>::value},
                  [runner](benchmark::State& state) { runner.template operator()<Layout>(state); });
}

} // namespace detail

/**
 * @brief Register the complete FieldPack layout matrix and its raw SoA case.
 *
 * @tparam FieldpackRunner Stateless callable with a templated layout operator.
 * @tparam RawRunner Stateless callable accepting `benchmark::State&`.
 * @return `true`, allowing one namespace-scope constant to own registration.
 */
template<class FieldpackRunner, class RawRunner>
auto register_benchmark_matrix(std::string_view kernel, FieldpackRunner fieldpack_runner, RawRunner raw_runner) -> bool
{
    detail::register_fieldpack_layout<fieldpack::soa>(kernel, fieldpack_runner);
    detail::register_fieldpack_layout<fieldpack::aosoa<16U>>(kernel, fieldpack_runner);
    detail::register_fieldpack_layout<fieldpack::aosoa<32U>>(kernel, fieldpack_runner);
    detail::register_fieldpack_layout<fieldpack::aosoa<64U>>(kernel, fieldpack_runner);
    detail::register_fieldpack_layout<fieldpack::aosoa<128U>>(kernel, fieldpack_runner);
    detail::register_case({.kernel = kernel, .implementation = "raw", .layout = "soa"}, std::move(raw_runner));
    return true;
}

} // namespace fieldpack_benchmark
