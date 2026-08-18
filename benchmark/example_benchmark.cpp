#include <benchmark/benchmark.h>
#include <my_project/example.hpp>

static void benchmark_add(benchmark::State& state)
{
    for (auto _ : state) {
        auto result = my_project::add(static_cast<int>(state.range(0)), 42);
        benchmark::DoNotOptimize(result);
    }
}

BENCHMARK(benchmark_add)->Arg(1)->Arg(1'000)->Arg(1'000'000);
