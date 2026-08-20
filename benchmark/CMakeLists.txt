include(FetchContent)

set(FIELDPACK_BENCHMARK_PRESET "custom" CACHE STRING
    "Name recorded in benchmark result metadata"
)
option(FIELDPACK_BENCHMARK_X86_64_NATIVE
    "Validate that this native benchmark build is configured on an x86-64 host"
    OFF
)

cmake_host_system_information(
    RESULT fieldpack_benchmark_host_processor
    QUERY OS_PLATFORM
)
if(FIELDPACK_BENCHMARK_X86_64_NATIVE
   AND NOT fieldpack_benchmark_host_processor MATCHES
       "^(x86_64|x64|AMD64|amd64)$")
    message(FATAL_ERROR
        "benchmark-x86-64-native requires an x86-64 host; detected "
        "${fieldpack_benchmark_host_processor}"
    )
endif()

# Google Benchmark recommends disabling its own tests and install rules when it
# is embedded with FetchContent.
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(google_benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(google_benchmark)

set(fieldpack_benchmark_effective_flags "${CMAKE_CXX_FLAGS}")
if(CMAKE_BUILD_TYPE)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" fieldpack_benchmark_build_type_upper)
    set(fieldpack_benchmark_config_flags_variable
        "CMAKE_CXX_FLAGS_${fieldpack_benchmark_build_type_upper}"
    )
    string(APPEND fieldpack_benchmark_effective_flags
        " ${${fieldpack_benchmark_config_flags_variable}}"
    )
endif()
string(STRIP "${fieldpack_benchmark_effective_flags}"
    fieldpack_benchmark_effective_flags
)
string(APPEND fieldpack_benchmark_effective_flags
    " -Wall -Wextra -Wpedantic -std=c++${CMAKE_CXX_STANDARD}"
)

# The runner reads this file without evaluating it as shell code. Keeping build
# metadata outside the executable lets the same workflow describe custom
# toolchains without embedding paths or flags into timed translation units.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmark_metadata.txt.in"
    "${CMAKE_CURRENT_BINARY_DIR}/benchmark_metadata.txt"
    @ONLY
)

# CONFIGURE_DEPENDS makes CMake reconfigure when benchmark files are added or
# removed. Each .cpp file becomes an independently runnable executable.
file(GLOB_RECURSE BENCHMARK_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

foreach(benchmark_source IN LISTS BENCHMARK_SOURCES)
    file(RELATIVE_PATH benchmark_relative
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${benchmark_source}"
    )
    string(REGEX REPLACE "\\.cpp$" "" benchmark_name "${benchmark_relative}")
    string(REPLACE "/" "_" benchmark_name "${benchmark_name}")
    set(benchmark_target "benchmark_${benchmark_name}")

    add_executable(${benchmark_target} "${benchmark_source}")
    target_link_libraries(${benchmark_target} PRIVATE
        ${PROJECT_NAME}::${PROJECT_NAME}
        ${PROJECT_NAME}_warnings
        ${PROJECT_NAME}_sanitizers
        benchmark::benchmark
        benchmark::benchmark_main
    )
endforeach()
