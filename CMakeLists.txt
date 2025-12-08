cmake_minimum_required(VERSION 3.20)
project(CFS C CXX)
set(CMAKE_CXX_STANDARD 23)
include_directories(src/include)
add_library(cfs_obj STATIC
        src/backtracer/general_cfs_error.cpp    src/include/general_cfs_error.h
        src/misc/color.cpp                      src/include/colors.h
        src/misc/utils.cpp                      src/include/utils.h
        src/backtracer/execute.cpp
)
add_executable(cfs src/main.cpp
        src/include/execute.h)
target_link_libraries(cfs PRIVATE cfs_obj)
target_compile_definitions(cfs PRIVATE
        CFS_IMPLEMENT_VERSION="0.0.1"
        CFS_STANDARD_VERSION="0.0.1"
)

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    if ("${SANITIZER}" STREQUAL "address")
        target_compile_options(cfs PRIVATE -fsanitize=address -fsanitize=undefined)
        target_link_libraries(cfs PRIVATE asan ubsan atomic)
    elseif("${SANITIZER}" STREQUAL "thread")
        target_compile_options(cfs PRIVATE -fsanitize=thread -fno-sanitize-recover=undefined -fno-sanitize=undefined)
        target_link_libraries(cfs PRIVATE tsan atomic)
    endif()
else ()
    string(REPLACE " " ";" CC_ADDITIONAL_OPTIONS_LIST "${CC_ADDITIONAL_OPTIONS}")
    string(REPLACE " " ";" LD_ADDITIONAL_OPTIONS_LIST "${LD_ADDITIONAL_OPTIONS}")
    target_compile_options(cfs PUBLIC
            -O3
            # -flto
            -fomit-frame-pointer
            -ffast-math
            -fstrict-aliasing
            -fdata-sections
            -ffunction-sections
            -D_FORTIFY_SOURCE=2
            -fstack-protector-strong
            -Wl,-z,relro -Wl,-z,now
            -s
            ${CC_ADDITIONAL_OPTIONS_LIST}
    )
    target_link_options(cfs PUBLIC
            -static-libgcc -static-libstdc++
            -O3
            # -flto
            -fomit-frame-pointer
            -ffast-math
            -fstrict-aliasing
            -fdata-sections
            -ffunction-sections
            -Wl,--gc-sections
            -D_FORTIFY_SOURCE=2
            -fstack-protector-strong
            -Wl,-z,relro -Wl,-z,now
            -s
            ${LD_ADDITIONAL_OPTIONS_LIST}
    )
    target_link_libraries(cfs PRIVATE atomic)
    set_target_properties(cfs PROPERTIES LINK_SEARCH_START_STATIC 1)
    set_target_properties(cfs PROPERTIES LINK_SEARCH_END_STATIC 1)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
endif ()
