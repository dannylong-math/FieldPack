cmake_minimum_required(VERSION 3.21)

project(FieldPack
    VERSION 0.1.0
    DESCRIPTION "Utilities to manage efficient data layout of parameter packs"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Prevent in-source builds
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not allowed.\n"
        "Run: cmake --preset debug  (or release / release-max)"
    )
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Symlink compile_commands.json into the source root for clangd
file(REMOVE "${CMAKE_SOURCE_DIR}/compile_commands.json")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E create_symlink
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json"
)

# Warnings — linked only to our targets so FetchContent deps are unaffected
add_library(${PROJECT_NAME}_warnings INTERFACE)
target_compile_options(${PROJECT_NAME}_warnings INTERFACE
    -Wall -Wextra -Wpedantic
)

# Sanitizers — empty unless ENABLE_SANITIZERS=ON (set by the debug preset)
add_library(${PROJECT_NAME}_sanitizers INTERFACE)
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)
if(ENABLE_SANITIZERS)
    target_compile_options(${PROJECT_NAME}_sanitizers INTERFACE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(${PROJECT_NAME}_sanitizers INTERFACE
        -fsanitize=address,undefined
    )
endif()

# Coverage — intended for test builds, enabled by the coverage CI job
add_library(${PROJECT_NAME}_coverage INTERFACE)
option(ENABLE_COVERAGE "Instrument tests for code coverage" OFF)
if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${PROJECT_NAME}_coverage INTERFACE
            --coverage
            -O0
            -g
        )
        target_link_options(${PROJECT_NAME}_coverage INTERFACE --coverage)
    else()
        message(FATAL_ERROR "ENABLE_COVERAGE requires GCC or Clang")
    endif()
endif()

# clang-tidy — enable with ENABLE_CLANG_TIDY=ON or add to a preset
option(ENABLE_CLANG_TIDY "Run clang-tidy during compilation" OFF)
if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE
        NAMES clang-tidy clang-tidy-22 clang-tidy-21 clang-tidy-20 clang-tidy-19 clang-tidy-18
        REQUIRED
    )
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

# Header-only interface library
add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_23)

option(ENABLE_OPTIMIZATION_REPORTS
    "Capture compiler optimization records and expose the optimization-report target"
    OFF
)
if(ENABLE_OPTIMIZATION_REPORTS)
    if(NOT CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo)$")
        message(FATAL_ERROR
            "ENABLE_OPTIMIZATION_REPORTS requires a Release or RelWithDebInfo build"
        )
    endif()

    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    include(CheckCXXCompilerFlag)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/optimization-records")

    add_executable(fieldpack_optimization_report_workload
        tools/optimization_report_workload.cpp
    )
    target_include_directories(fieldpack_optimization_report_workload PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmark"
    )
    target_link_libraries(fieldpack_optimization_report_workload PRIVATE
        ${PROJECT_NAME}::${PROJECT_NAME}
        ${PROJECT_NAME}_warnings
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        check_cxx_compiler_flag(
            "-fsave-optimization-record=yaml"
            FIELDPACK_HAS_CLANG_OPTIMIZATION_RECORDS
        )
        check_cxx_compiler_flag(
            "-Rpass=loop-vectorize"
            FIELDPACK_HAS_CLANG_VECTOR_REMARKS
        )
        check_cxx_compiler_flag(
            "-foptimization-record-passes=loop-vectorize"
            FIELDPACK_HAS_CLANG_RECORD_FILTER
        )
        if(NOT FIELDPACK_HAS_CLANG_OPTIMIZATION_RECORDS
           OR NOT FIELDPACK_HAS_CLANG_VECTOR_REMARKS
           OR NOT FIELDPACK_HAS_CLANG_RECORD_FILTER)
            message(FATAL_ERROR
                "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} does not support the "
                "serialized optimization-record and vector-remark flags required by this preset"
            )
        endif()
        set(fieldpack_optimization_record
            "${CMAKE_BINARY_DIR}/optimization-records/fieldpack.opt.yaml"
        )
        set(fieldpack_optimization_capture_flags
            -fsave-optimization-record=yaml
            "-foptimization-record-file=${fieldpack_optimization_record}"
            "-foptimization-record-passes=loop-vectorize|slp-vectorizer|inline"
            -Rpass=loop-vectorize
            -Rpass-missed=loop-vectorize
            -Rpass-analysis=loop-vectorize
        )
        target_compile_options(fieldpack_optimization_report_workload PRIVATE
            ${fieldpack_optimization_capture_flags}
        )
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        check_cxx_compiler_flag(
            "-fsave-optimization-record"
            FIELDPACK_HAS_GCC_OPTIMIZATION_RECORDS
        )
        check_cxx_compiler_flag(
            "-fopt-info-vec-all"
            FIELDPACK_HAS_GCC_VECTOR_REPORTS
        )
        if(NOT FIELDPACK_HAS_GCC_OPTIMIZATION_RECORDS
           OR NOT FIELDPACK_HAS_GCC_VECTOR_REPORTS)
            message(FATAL_ERROR
                "GCC ${CMAKE_CXX_COMPILER_VERSION} does not support the machine-readable record "
                "and vector-diagnostic flags required by this preset"
            )
        endif()
        set(fieldpack_optimization_capture_flags
            -fsave-optimization-record
            "-fopt-info-vec-all=${CMAKE_BINARY_DIR}/optimization-records/vectorization.txt"
        )
        target_compile_options(fieldpack_optimization_report_workload PRIVATE
            ${fieldpack_optimization_capture_flags}
        )
    else()
        message(FATAL_ERROR
            "ENABLE_OPTIMIZATION_REPORTS supports Clang and GCC; detected "
            "${CMAKE_CXX_COMPILER_ID}"
        )
    endif()

    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" --version
        OUTPUT_VARIABLE fieldpack_optimization_compiler_version
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    string(REPLACE "\n" " | " fieldpack_optimization_compiler_version
        "${fieldpack_optimization_compiler_version}"
    )
    string(JOIN " " fieldpack_optimization_capture_command
        "${CMAKE_CXX_COMPILER}"
        "${CMAKE_CXX_FLAGS}"
        "${CMAKE_CXX_FLAGS_RELEASE}"
        ${fieldpack_optimization_capture_flags}
    )

    add_custom_target(optimization-report
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/optimization_report.py"
            --input-dir "${CMAKE_BINARY_DIR}"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}"
            --output "${CMAKE_BINARY_DIR}/report"
            --compiler-version "${fieldpack_optimization_compiler_version}"
            --command "${fieldpack_optimization_capture_command}"
        DEPENDS fieldpack_optimization_report_workload
        BYPRODUCTS "${CMAKE_BINARY_DIR}/report/index.html"
        COMMENT "Generating FieldPack optimization report"
        VERBATIM
        USES_TERMINAL
    )
endif()

option(BUILD_TESTING "Build tests" ${PROJECT_IS_TOP_LEVEL})
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# Build examples by default only when FieldPack is the top-level project. This
# keeps FetchContent and add_subdirectory consumers free of example targets
# unless they explicitly opt in.
option(BUILD_EXAMPLES "Build FieldPack examples" ${PROJECT_IS_TOP_LEVEL})
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

option(BUILD_BENCHMARKS "Build Google Benchmark executables" OFF)
if(BUILD_BENCHMARKS)
    add_subdirectory(benchmark)
endif()

install(TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}Targets
)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(EXPORT ${PROJECT_NAME}Targets
    FILE ${PROJECT_NAME}Targets.cmake
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FieldPackConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/FieldPackConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/FieldPackConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/FieldPackConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/FieldPackConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
)
