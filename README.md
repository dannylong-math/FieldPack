# FieldPack

[![Unit tests](https://github.com/dannylong-math/FieldPack/actions/workflows/ci.yml/badge.svg?branch=main&event=push)](https://github.com/dannylong-math/FieldPack/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/dannylong-math/FieldPack/graph/badge.svg)](https://codecov.io/gh/dannylong-math/FieldPack)
[![Documentation](https://github.com/dannylong-math/FieldPack/actions/workflows/docs.yml/badge.svg?branch=main&event=push)](https://github.com/dannylong-math/FieldPack/actions/workflows/docs.yml)

FieldPack is a C++23 header-only library for defining named field schemas and
storing their values in interchangeable data layouts. Its goal is to let the
same numerical kernel operate on Structure of Arrays (SoA) and tiled Array of
Structures of Arrays (AoSoA) storage, making layout and execution choices
independent of application code.

The generated API reference is available on the
[FieldPack documentation site](https://dannylong-math.github.io/FieldPack/).

## Quickstart

FieldPack requires CMake 3.21 or newer and a C++23 compiler. Link the
header-only `FieldPack::FieldPack` target to any target that uses the library.

### Git submodule

Add FieldPack to your repository:

```shell
git submodule add https://github.com/dannylong-math/FieldPack.git external/FieldPack
git submodule update --init --recursive
```

Then include it from your `CMakeLists.txt`:

```cmake
add_subdirectory(external/FieldPack)

add_executable(my_application main.cpp)
target_link_libraries(my_application PRIVATE FieldPack::FieldPack)
```

The submodule records the exact FieldPack commit used by your project. Update
it deliberately when you want to adopt a newer revision.

### FetchContent

CMake can also download FieldPack during configuration:

```cmake
include(FetchContent)

FetchContent_Declare(
    FieldPack
    GIT_REPOSITORY https://github.com/dannylong-math/FieldPack.git
    GIT_TAG main
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(FieldPack)

add_executable(my_application main.cpp)
target_link_libraries(my_application PRIVATE FieldPack::FieldPack)
```

For reproducible builds, replace `main` with a release tag or full commit hash.
Configure with `BUILD_TESTING=OFF` when dependency tests should not be built.

## Using FieldPack

The umbrella header provides the complete public API. Define empty tag types,
associate each tag with a value type, and select a layout when constructing a
table:

```cpp
#include <cstddef>
#include <cstdint>

// The umbrella header is the recommended user-facing include.
#include <fieldpack/fieldpack.hpp>

namespace {

struct x {};
struct velocity_x {};
struct id {};

using particle_schema = fieldpack::schema<fieldpack::field<x, float>, fieldpack::field<velocity_x, float>,
                                          fieldpack::field<id, std::uint32_t>>;

using soa_particles = fieldpack::table<particle_schema, fieldpack::soa>;
using aosoa_particles = fieldpack::table<particle_schema, fieldpack::aosoa<64>>;

} // namespace

int main() 
{
    // This size produces eight full chunks of eight and one tail of three.
    aosoa_particles particles(67);

    auto first = particles.at(0);
    first.get<x>() = 2.0F;
    first.get<velocity_x>() = 0.5F;
    first.get<id>() = 0U;

    using drift_fields = fieldpack::field_access<fieldpack::mutate<x>, fieldpack::read<velocity_x>>;

    fieldpack::for_each_chunk<8>(particles, drift_fields{}, [](auto chunk) {
        auto positions = chunk.template get<x>();
        const auto velocities = chunk.template get<velocity_x>();

        // Both spans have the callback-provided chunk size.
        for (std::size_t lane = 0; lane < chunk.size(); ++lane) {
            positions[lane] += velocities[lane];
        }
    });
}
```

The same traversal function accepts an SoA table without changing the kernel.
Read descriptors expose const spans, while mutate descriptors expose writable
spans. The complete, executable version is
[`examples/quickstart.cpp`](examples/quickstart.cpp). The shorter snippet above
is also compiled verbatim as
[`examples/documented_quickstart.cpp`](examples/documented_quickstart.cpp).

## Tiles, chunks, and tails

These terms describe different layers of the library:

| Term | Meaning |
| --- | --- |
| **Tile** | An AoSoA physical storage block containing `TileExtent` consecutive values for every schema field. SoA storage has no tiles. |
| **Chunk** | Up to `ChunkExtent` consecutive logical records passed to one kernel callback as named spans. |
| **Tail** | The final chunk when fewer than `ChunkExtent` live records remain. It contains only live values and never exposes AoSoA padding. |

For AoSoA traversal, `ChunkExtent` must divide `TileExtent`, ensuring that a
full chunk never crosses a physical tile boundary. SoA accepts every positive
chunk extent.

## Lifetime and invalidation

Tables own their field storage. Row proxies returned by `operator[]` or `at()`,
and spans or chunk bundles supplied during traversal, are non-owning views into
that storage. Do not retain them across an operation that may invalidate the
table.

Resizing—including resizing to the current size—assigning, moving, swapping,
or destroying a table invalidates its existing row proxies, references, spans,
and chunk bundles. A callback should normally consume its named spans before it
returns.

## Supported configurations

FieldPack requires C++23 and is currently tested with GCC and Clang on x86-64
Linux. Other compilers and architectures are not yet part of the supported test
matrix.

Schema values may be non-cv, trivially copyable arithmetic types other than
`bool`. Schemas must be non-empty and use each exact tag type only once.

## Building and running the tests

Configure the debug preset, build the project, and run CTest from the repository
root:

```shell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

The debug preset enables AddressSanitizer and UndefinedBehaviorSanitizer. The
test suite is compiled as C++23 and is intended to pass with both GCC and
Clang. When FieldPack is configured as the top-level project, the executable
quickstart is also built and registered with CTest. Set `BUILD_EXAMPLES=OFF` to
disable example targets explicitly.

## Running the benchmarks

Build the portable Release benchmark suite and save a labeled JSON result set:

```shell
cmake --preset benchmark-portable
cmake --build --preset benchmark-portable
./benchmark/run_benchmarks.sh benchmark-portable baseline
```

An x86-64-only preset is also available with `-march=native` and
`-mtune=native`:

```shell
cmake --preset benchmark-x86-64-native
cmake --build --preset benchmark-x86-64-native
```

Benchmark results are informational and have no timing thresholds. See the
[benchmark guide](https://dannylong-math.github.io/FieldPack/benchmarks/) for
the kernel matrix, recorded metadata, result comparison commands, and
measurement guidance.

## Inspecting compiler optimizations

Generate a self-contained HTML report from Clang optimization remarks with:

```shell
cmake --preset optimization-report-clang
cmake --build --preset optimization-report-clang --target optimization-report
```

Use `optimization-report-gcc` for GCC. The report target prints the path to its
local `index.html`; report flags and artifacts remain isolated from normal and
benchmark builds. See the
[optimization-report guide](https://dannylong-math.github.io/FieldPack/optimization-reports/)
for filters and interpretation guidance.
