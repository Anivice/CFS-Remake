cmake_minimum_required(VERSION 3.30)

set(PNG_STATIC "ON")
set(PNG_SHARED "OFF")
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/libpng)

project(tiv CXX)
add_library(tiv STATIC CImg.h tiv_lib.cpp tiv_lib.h libtiv.cpp embedded_pic.cpp)
target_compile_definitions(tiv PUBLIC cimg_use_png=1)
target_compile_options(tiv PRIVATE -std=c++17 -Wall -fexceptions)
target_include_directories(tiv PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libpng ${CMAKE_CURRENT_BINARY_DIR}/libpng)
target_link_libraries(tiv PRIVATE png_static)
add_dependencies(tiv png_static)
