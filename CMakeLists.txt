cmake_minimum_required(VERSION 3.20)
project(CFS C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CFS_IMPLEMENT_VERSION "0.0.1")
set(CFS_STANDARD_VERSION "0.0.1")
include_directories(src/include)
if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    add_compile_definitions(__DEBUG__=1)
endif ()

add_compile_definitions(
        CFS_IMPLEMENT_VERSION="${CFS_IMPLEMENT_VERSION}"
        CFS_STANDARD_VERSION="${CFS_STANDARD_VERSION}"
)

execute_process(
        COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_BINARY_DIR}/version.cpp"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_library(cfs_obj STATIC                      src/include/cfs.h
        src/backtracer/generalCFSbaseError.cpp  src/include/generalCFSbaseError.h
        src/misc/color.cpp                      src/include/colors.h
        src/misc/utils.cpp                      src/include/utils.h
        src/misc/execute.cpp                    src/include/execute.h
        src/misc/logger.cpp                     src/include/logger.h
        src/cfs/mmap.cpp                        src/include/mmap.h
        src/cfs/smart_block_t.cpp               src/include/smart_block_t.h
        src/cfs/cfsBasicComponents.cpp          src/include/cfsBasicComponents.h
        src/misc/args.cpp                       src/include/args.h
)
add_executable(cfs src/main.cpp src/bin/fsck.cpp ${CMAKE_BINARY_DIR}/version.cpp)
target_link_libraries(cfs PRIVATE cfs_obj)

find_program(XXD_EXEC xxd)
add_custom_target(version_cpp ALL
        COMMAND     /bin/sh -c "\"${XXD_EXEC}\" -i -n version_text < \"${CMAKE_SOURCE_DIR}\"/src/res/version.txt > \"${CMAKE_BINARY_DIR}\"/version.cpp"
        COMMENT     "Generate version.cpp..."
        BYPRODUCTS  "${CMAKE_BINARY_DIR}/version.cpp"
        VERBATIM
)

set(UNIT_TESTS "")

# add_unit_test EXECUTABLE_NAME [FILES...]
function(add_unit_test EXECUTABLE_NAME)
    add_executable(${EXECUTABLE_NAME} ${ARGN})
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE cfs_obj)
    if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
        if ("${SANITIZER}" STREQUAL "address")
            target_compile_options(${EXECUTABLE_NAME} PRIVATE -fsanitize=address -fsanitize=undefined)
            target_link_libraries(${EXECUTABLE_NAME} PRIVATE asan ubsan atomic)
        elseif("${SANITIZER}" STREQUAL "thread")
            target_compile_options(${EXECUTABLE_NAME} PRIVATE -fsanitize=thread -fno-sanitize-recover=undefined -fno-sanitize=undefined)
            target_link_libraries(${EXECUTABLE_NAME} PRIVATE tsan atomic)
        endif()
    endif()

    add_custom_target(SYMBOL_LINK_FOR_TEST_${EXECUTABLE_NAME} ALL
            COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_CURRENT_BINARY_DIR}/${EXECUTABLE_NAME} ${CMAKE_BINARY_DIR}/${EXECUTABLE_NAME}.test
            COMMENT "Creating symbol link ${CMAKE_BINARY_DIR}/${EXECUTABLE_NAME}.test => ${CMAKE_CURRENT_BINARY_DIR}/${EXECUTABLE_NAME}"
            DEPENDS ${EXECUTABLE_NAME}
            BYPRODUCTS ${CMAKE_BINARY_DIR}/${EXECUTABLE_NAME}.test)

    list(APPEND UNIT_TESTS "SYMBOL_LINK_FOR_TEST_${EXECUTABLE_NAME}")
    set(UNIT_TESTS "${UNIT_TESTS}" PARENT_SCOPE)
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
else ()
    if ("${USE_ADDITIONAL_FLAGS}" STREQUAL "True")
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
    endif ()
    target_link_libraries(cfs PRIVATE atomic)
    set_target_properties(cfs PROPERTIES LINK_SEARCH_START_STATIC 1)
    set_target_properties(cfs PROPERTIES LINK_SEARCH_END_STATIC 1)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
endif ()

enable_testing()

add_unit_test(bitmap src/tests/bitmap.cpp)
add_unit_test(blocks src/tests/blocks.cpp)
add_unit_test(cfs_comp_bitmaps src/tests/cfs_comp_bitmaps.cpp)
add_unit_test(journal src/tests/journal.cpp)

set(ZLIB_BUILD_EXAMPLES OFF)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/imgInTerm/libpng/zlib-1.3.1)
add_library(ZLIB::ZLIB ALIAS zlib)
set(ZLIB_LOCAL_INC ${CMAKE_SOURCE_DIR}/src/imgInTerm/libpng)

add_subdirectory(src/imgInTerm)
add_executable(test_all src/tests/test.cpp)
target_compile_definitions(test_all PRIVATE CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}")
target_link_libraries(test_all PRIVATE cfs_obj tiv)
add_test(NAME "test" COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_all)
add_dependencies(test_all ${UNIT_TESTS})
foreach (test ${UNIT_TESTS})
    message(STATUS ${test})
endforeach ()