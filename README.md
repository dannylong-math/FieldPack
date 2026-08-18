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

Include the headers needed by your code:

```cpp
#include <fieldpack/schema.hpp>

struct x {};
struct id {};

using particle_schema = fieldpack::schema<
    fieldpack::field<x, float>,
    fieldpack::field<id, unsigned>>;

static_assert(fieldpack::valid_schema<particle_schema>);
```

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
Clang.
