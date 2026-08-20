include(FetchContent)

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)

FetchContent_Declare(ut
    GIT_REPOSITORY https://github.com/boost-ext/ut.git
    GIT_TAG v2.3.1
)
FetchContent_MakeAvailable(ut)

# Boost.UT is a third-party dependency. Treat its headers as system headers so
# compiler and clang-tidy diagnostics remain scoped to FieldPack-owned code.
get_target_property(ut_include_directories ut INTERFACE_INCLUDE_DIRECTORIES)
set_property(TARGET ut PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${ut_include_directories}")

# Test executables use the test_*.cpp naming convention. Keeping fixtures and
# external-consumer sources outside this glob lets those files be compiled by
# their dedicated workflows instead of accidentally becoming unit targets.
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/headers/test_*.cpp"
)

foreach(test_source IN LISTS TEST_SOURCES)
    # NAME_WE: filename Without Extension (e.g. test_foo.cpp -> test_foo)
    get_filename_component(test_name "${test_source}" NAME_WE)
    add_executable(${test_name} "${test_source}")
    target_link_libraries(${test_name} PRIVATE
        ${PROJECT_NAME}::${PROJECT_NAME}
        ${PROJECT_NAME}_warnings
        ${PROJECT_NAME}_sanitizers
        ${PROJECT_NAME}_coverage
        Boost::ut
    )
    if(test_name STREQUAL "test_benchmark_contract")
        target_include_directories(${test_name} PRIVATE "${PROJECT_SOURCE_DIR}/benchmark")
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

add_test(
    NAME test_optimization_report
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s "${CMAKE_CURRENT_SOURCE_DIR}/optimization_report"
        -p "test_*.py"
        -v
)

add_test(
    NAME test_optimization_report_cmake
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/optimization_report/test_cmake_integration.py"
        --repository "${PROJECT_SOURCE_DIR}"
        --compiler "${CMAKE_CXX_COMPILER}"
        --cmake "${CMAKE_COMMAND}"
        --generator "${CMAKE_GENERATOR}"
)
set_tests_properties(test_optimization_report_cmake PROPERTIES
    LABELS integration
    TIMEOUT 180
)

add_test(
    NAME test_installed_consumer
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/consumer/test_installed_consumer.py"
        --repository "${PROJECT_SOURCE_DIR}"
        --project-build "${PROJECT_BINARY_DIR}"
        --compiler "${CMAKE_CXX_COMPILER}"
        --cmake "${CMAKE_COMMAND}"
        --generator "${CMAKE_GENERATOR}"
)
set_tests_properties(test_installed_consumer PROPERTIES
    LABELS integration
    TIMEOUT 120
)

add_test(
    NAME test_documented_examples
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/documented/test_documented_examples.py"
        --repository "${PROJECT_SOURCE_DIR}"
)
