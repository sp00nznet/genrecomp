# This is built by the top-level CMakeLists.txt when
# GENRECOMP_BUILD_DEBUG_RUNNER=ON. This file exists for standalone use.

cmake_minimum_required(VERSION 3.16)
project(genrecomp_debug_runner C)

find_package(SDL2 REQUIRED)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. genrecomp_build)

add_executable(debug_runner main.c)
target_link_libraries(debug_runner PRIVATE genrecomp SDL2::SDL2main)
target_compile_definitions(debug_runner PRIVATE GENRECOMP_DEBUG=1)
