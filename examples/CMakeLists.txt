add_executable(fieldpack_quickstart quickstart.cpp)
target_link_libraries(fieldpack_quickstart PRIVATE
    ${PROJECT_NAME}::${PROJECT_NAME}
    ${PROJECT_NAME}_warnings
    ${PROJECT_NAME}_sanitizers
)

if(BUILD_TESTING)
    add_test(NAME example_quickstart COMMAND fieldpack_quickstart)
endif()
