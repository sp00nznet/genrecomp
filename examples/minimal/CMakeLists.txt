# This example is built by the top-level CMakeLists.txt when
# GENRECOMP_BUILD_EXAMPLES=ON. This file exists for standalone use.

cmake_minimum_required(VERSION 3.16)
project(genrecomp_minimal_example C)

find_package(SDL2 REQUIRED)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. genrecomp_build)

add_executable(minimal main.c)
target_link_libraries(minimal PRIVATE genrecomp SDL2::SDL2main)
