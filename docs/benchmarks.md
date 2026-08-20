# Benchmarks

FieldPack's benchmark suite compares the same correctness-tested numerical
kernels across portable SoA, tiled AoSoA, and hand-written raw SoA storage. The
suite is informational: it never enforces timing thresholds in CI or treats a
benchmark result as a correctness test.

## Kernels and matrix

Each executable registers `soa`, `aosoa<16>`, `aosoa<32>`, `aosoa<64>`,
`aosoa<128>`, and a raw SoA baseline at sizes 63, 64, 65, 2^10, 2^15, and
2^20.

| Executable | Kernel | Logical bytes per item |
| --- | --- | ---: |
| `benchmark_polynomial_evaluation` | Cubic Horner evaluation | 48 |
| `benchmark_drift` | Two-dimensional position update | 48 |
| `benchmark_field_subset` | Two hot fields in an eight-field schema | 24 |
| `benchmark_read_only_reduction` | Explicit-order four-field checksum | 32 |

Logical byte counts represent field reads and writes expressed by the kernel.
They are not measurements of cache-line transfers or memory-controller
traffic. All arithmetic fields in this initial suite are `double`, and
FieldPack kernels use a chunk extent of 8.

## Build presets

Use the portable preset when results may be compared across compatible
machines or when a baseline must avoid host-specific instructions:

```shell
cmake --preset benchmark-portable
cmake --build --preset benchmark-portable
```

On an x86-64 benchmark host, the native preset adds `-march=native` and
`-mtune=native`:

```shell
cmake --preset benchmark-x86-64-native
cmake --build --preset benchmark-x86-64-native
```

The native preset refuses to configure on a non-x86-64 host. Its output is
specific to the compiler and CPU that produced it and should not be presented
as a portable result. The older `benchmark` preset remains a compatibility
alias for `benchmark-portable`.

## Save a repeatable run

The runner requires a unique label and refuses to overwrite an existing result
directory. It uses Python 3's standard library to add exact build metadata to
Google Benchmark's JSON output:

```shell
./benchmark/run_benchmarks.sh benchmark-portable baseline
./benchmark/run_benchmarks.sh benchmark-portable contender
```

Additional Google Benchmark options are forwarded to every executable. For a
statistical comparison, repetitions can be requested explicitly:

```shell
./benchmark/run_benchmarks.sh benchmark-portable baseline-10 \
  --benchmark_repetitions=10 \
  --benchmark_report_aggregates_only=true
```

Results are written beneath the selected build directory:

```text
build/benchmark-portable/results/baseline/
├── polynomial.json
├── drift.json
├── field_subset.json
└── read_only_reduction.json
```

Every invocation adds the run label, preset, build type, compiler identity,
compiler version, compiler path, effective flags, host architecture, CPU model,
chunk extent, and field types to the JSON context. Google Benchmark also
records timing, CPU count and frequency, cache descriptions, load averages,
and its own version. Each result name has this stable form:

```text
kernel/implementation/layout/problem_size
```

For example, `polynomial/fieldpack/aosoa<64>/32768` records both the tile extent
and problem size. `polynomial/raw/soa/32768` identifies the corresponding raw
baseline.

An individual executable can still be run directly:

```shell
./build/benchmark-portable/benchmark/benchmark_polynomial_evaluation \
  --benchmark_out=build/benchmark-portable/polynomial.json \
  --benchmark_out_format=json
```

Prefer `run_benchmarks.sh` for archived results because it adds the complete
custom metadata context and protects existing run labels.

## Compare results

The comparison wrapper locates `tools/compare.py` in the Google Benchmark
source fetched by CMake:

```shell
./benchmark/compare_results.sh \
  benchmark-portable baseline contender polynomial
```

The wrapper does not install Python packages. If NumPy or SciPy is missing, it
prints commands for an isolated environment using Google Benchmark's pinned
requirements file.

```shell
python3 -m venv .venv-benchmark
.venv-benchmark/bin/python -m pip install -r \
  build/benchmark-portable/_deps/google_benchmark-src/tools/requirements.txt
source .venv-benchmark/bin/activate
```

To compare two layouts within one saved result, use the wrapper's layout mode:

```shell
./benchmark/compare_results.sh layouts \
  benchmark-portable baseline polynomial \
  'polynomial/raw/soa' 'polynomial/fieldpack/aosoa<64>'
```

The filters are regular expressions understood by Google Benchmark's
comparison tool. They select all matching problem sizes without recompiling or
editing benchmark source.

## Measurement discipline

Close background work, use a consistent power policy, and record whether CPU
frequency scaling is enabled. Compare results produced by the same executable,
compiler flags, and machine unless the changed item is deliberately under
study. Short smoke runs prove only that the workflow executes; they are not
evidence of a performance difference.
