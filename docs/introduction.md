# FieldPack

FieldPack is a C++23 header-only library for defining named field schemas and
storing their values in interchangeable Structure of Arrays (SoA) and tiled
Array of Structures of Arrays (AoSoA) layouts. A numerical kernel names the
fields it reads and mutates without depending on the table's physical layout.

## Quickstart

Include the umbrella header, define an empty tag for each field, and associate
the tags with supported arithmetic value types:

```cpp
#include <cstddef>
#include <cstdint>
#include <fieldpack/fieldpack.hpp>

struct x {};
struct velocity_x {};
struct id {};

using particle_schema = fieldpack::schema<
    fieldpack::field<x, float>,
    fieldpack::field<velocity_x, float>,
    fieldpack::field<id, std::uint32_t>>;

using soa_particles =
    fieldpack::table<particle_schema, fieldpack::soa>;

using aosoa_particles =
    fieldpack::table<particle_schema, fieldpack::aosoa<64>>;
```

Both layouts expose the same logical row interface. `at()` checks its index,
while `operator[]` requires a valid index:

```cpp
aosoa_particles particles(67);

auto first = particles.at(0);
first.get<x>() = 2.0F;
first.get<velocity_x>() = 0.5F;
first.get<id>() = 0U;
```

## Named chunk traversal

An access list gives every field an explicit read or mutation permission. The
kernel retrieves spans by exact tag rather than schema position:

```cpp
using drift_fields = fieldpack::field_access<
    fieldpack::mutate<x>,
    fieldpack::read<velocity_x>>;

fieldpack::for_each_chunk<8>(
    particles,
    drift_fields{},
    [](auto chunk) {
        auto positions = chunk.template get<x>();
        const auto velocities = chunk.template get<velocity_x>();

        for (std::size_t lane = 0; lane < chunk.size(); ++lane) {
            positions[lane] += velocities[lane];
        }
    });
```

`read<Tag>` produces a const span even when the table is mutable.
`mutate<Tag>` produces a writable span and is rejected for const tables. The
same kernel works with an SoA table or any compatible AoSoA table.

The repository's [`examples/quickstart.cpp`](https://github.com/dannylong-math/FieldPack/blob/main/examples/quickstart.cpp)
is compiled and run as part of the test suite.

## Tile, chunk, and tail

The three terms belong to two distinct layers:

| Term | Layer | Meaning |
| --- | --- | --- |
| **Tile** | AoSoA storage | A physical block containing `TileExtent` consecutive values for every schema field. Tile objects and padding are internal. |
| **Chunk** | Execution | Up to `ChunkExtent` consecutive logical records passed to one callback as named spans. |
| **Tail** | Execution | The final chunk when fewer than `ChunkExtent` logical records remain. It exposes only live records. |

For AoSoA traversal, `ChunkExtent` must divide `TileExtent`. This prevents a
full chunk from crossing a physical tile boundary. SoA has no physical tile
constraint and accepts every positive chunk extent.

With 67 records and a chunk extent of 8, traversal invokes eight full chunks
followed by one three-record tail. Even if an AoSoA allocation contains padding
after those records, the tail never exposes it.

## Lifetime and invalidation

A table owns its storage. These objects are non-owning views into that table:

- Row proxies returned by `operator[]` and `at()`.
- Field references returned by a row proxy.
- Named spans and chunk bundles supplied to traversal callbacks.

Resizing—including resizing to the current size—assigning, moving, swapping,
or destroying the source table invalidates all existing proxies, references,
spans, and chunk bundles into it. Consume traversal spans during their callback
unless the table's lifetime and lack of invalidating operations are otherwise
strictly controlled.

## Supported configurations

FieldPack requires C++23 and is currently tested with GCC and Clang on x86-64
Linux. Other compilers and architectures are not yet part of the supported test
matrix.

A schema must be non-empty and use unique exact tag types. Field values may be
non-cv, trivially copyable arithmetic types other than `bool`.
