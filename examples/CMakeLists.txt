add_executable(fieldpack_quickstart quickstart.cpp)
target_link_libraries(fieldpack_quickstart PRIVATE
    ${PROJECT_NAME}::${PROJECT_NAME}
    ${PROJECT_NAME}_warnings
    ${PROJECT_NAME}_sanitizers
)

add_executable(fieldpack_documented_quickstart documented_quickstart.cpp)
target_link_libraries(fieldpack_documented_quickstart PRIVATE
    ${PROJECT_NAME}::${PROJECT_NAME}
    ${PROJECT_NAME}_warnings
    ${PROJECT_NAME}_sanitizers
)

if(BUILD_TESTING)
    add_test(NAME example_quickstart COMMAND fieldpack_quickstart)
    add_test(NAME example_documented_quickstart COMMAND fieldpack_documented_quickstart)
endif()
