cmake_minimum_required(VERSION 3.21)

project(FieldPackInstalledConsumer LANGUAGES CXX)

find_package(FieldPack 0.1 CONFIG REQUIRED)

add_executable(fieldpack_installed_consumer main.cpp)
target_link_libraries(fieldpack_installed_consumer PRIVATE FieldPack::FieldPack)
target_compile_features(fieldpack_installed_consumer PRIVATE cxx_std_23)
