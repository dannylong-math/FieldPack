include(FetchContent)

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
