cmake_minimum_required(VERSION 3.20)
project(CFS C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CFS_IMPLEMENT_VERSION "0.0.1")
set(CFS_STANDARD_VERSION "0.0.1")
include_directories(src/include)
add_library(cfs_obj STATIC
        src/backtracer/generalCFSbaseError.cpp  src/include/generalCFSbaseError.h
        src/misc/color.cpp                      src/include/colors.h
        src/misc/utils.cpp                      src/include/utils.h
        src/misc/execute.cpp                    src/include/execute.h
        src/misc/logger.cpp                     src/include/logger.h
        src/cfs/mmap.cpp                        src/include/mmap.h
        src/cfs/smart_block_t.cpp               src/include/smart_block_t.h
)
add_executable(cfs src/main.cpp)
target_link_libraries(cfs PRIVATE cfs_obj)
target_compile_definitions(cfs PUBLIC
        CFS_IMPLEMENT_VERSION="${CFS_IMPLEMENT_VERSION}"
        CFS_STANDARD_VERSION="${CFS_STANDARD_VERSION}"
)
target_compile_definitions(cfs_obj PUBLIC
        CFS_IMPLEMENT_VERSION="${CFS_IMPLEMENT_VERSION}"
        CFS_STANDARD_VERSION="${CFS_STANDARD_VERSION}"
)

# add_unit_test EXECUTABLE_NAME [FILES...]
function(add_unit_test EXECUTABLE_NAME)
    add_executable(${EXECUTABLE_NAME} ${ARGN})
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE cfs_obj)
    if ("${SANITIZER}" STREQUAL "address")
        target_compile_options(${EXECUTABLE_NAME} PRIVATE -fsanitize=address -fsanitize=undefined)
        target_link_libraries(${EXECUTABLE_NAME} PRIVATE asan ubsan atomic)
    elseif("${SANITIZER}" STREQUAL "thread")
        target_compile_options(${EXECUTABLE_NAME} PRIVATE -fsanitize=thread -fno-sanitize-recover=undefined -fno-sanitize=undefined)
        target_link_libraries(${EXECUTABLE_NAME} PRIVATE tsan atomic)
    endif()

    add_test(NAME "test_${EXECUTABLE_NAME}" COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${EXECUTABLE_NAME})
    message(STATUS "Unit test `test_${EXECUTABLE_NAME}` enabled")
endfunction()

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    target_compile_definitions(cfs_obj PUBLIC DEBUG=1)
    target_compile_definitions(cfs PUBLIC DEBUG=1)
    if ("${SANITIZER}" STREQUAL "address")
        target_compile_options(cfs PRIVATE -fsanitize=address -fsanitize=undefined)
        target_link_libraries(cfs PRIVATE asan ubsan atomic)
    elseif("${SANITIZER}" STREQUAL "thread")
        target_compile_options(cfs PRIVATE -fsanitize=thread -fno-sanitize-recover=undefined -fno-sanitize=undefined)
        target_link_libraries(cfs PRIVATE tsan atomic)
    endif()

    enable_testing()
    add_unit_test(bitmap src/tests/bitmap.cpp)
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
