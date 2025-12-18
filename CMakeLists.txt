cmake_minimum_required(VERSION 3.20)
project(CFS C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 23)
set(CFS_IMPLEMENT_VERSION "0.0.1")
set(CFS_STANDARD_VERSION "0.0.1")

# USE_ADDITIONAL_FLAGS                   - Use additional compiler and linker flags for release builds
# NCURSES_CONFIGURE_ADDITIONAL_FLAGS     - ncurses additional configuration flags
# NCURSES_MAKE_ADDITIONAL_FLAGS          - ncurses additional make build flags
# NCURSES_MAKE_INSTALL_ADDITIONAL_FLAGS  - ncurses additional make install flags
# READLINE_MAKE_ADDITIONAL_FLAGS         - readline additional configuration flags
# READLINE_CONFIGURE_ADDITIONAL_FLAGS    - readline additional make build flags
# READLINE_MAKE_INSTALL_ADDITIONAL_FLAGS - readline additional make install flags
# CC_ADDITIONAL_OPTIONS                  - Use additional compiler flags for release builds
# LD_ADDITIONAL_OPTIONS                  - Use additional linker flags for release builds
# DEBUG_TIV_SHOW                         - Generate instance for debugging void show()

# readline only supports Makefile
find_program(MAKE_EXECUTABLE NAMES make gmake REQUIRED)
include(ExternalProject)
set(NCURSES_MAKE_ENTIRE "${MAKE_EXECUTABLE} ${NCURSES_MAKE_ADDITIONAL_FLAGS}")
message(STATUS "NCRUSES: ${NCURSES_MAKE_ENTIRE}")
ExternalProject_Add(NCURSES
        PREFIX              ${CMAKE_CURRENT_BINARY_DIR}/ncurses
        SOURCE_DIR          ${CMAKE_SOURCE_DIR}/src/NonCMake/ncurses-6.5
        INSTALL_DIR         ${CMAKE_CURRENT_BINARY_DIR}/ncurses/install
        CONFIGURE_COMMAND   <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> ${NCURSES_CONFIGURE_ADDITIONAL_FLAGS}
        BUILD_COMMAND       /bin/sh -c "${NCURSES_MAKE_ENTIRE}"
        INSTALL_COMMAND     ${MAKE_EXECUTABLE} install ${NCURSES_MAKE_INSTALL_ADDITIONAL_FLAGS}
        INSTALL_BYPRODUCTS  <INSTALL_DIR>/lib/libncursesw.a
        TEST_COMMAND        ""
        INACTIVITY_TIMEOUT  15
        CONFIGURE_HANDLED_BY_BUILD ON
)

ExternalProject_Get_Property(NCURSES INSTALL_DIR)
set(NCURSES_INCLUDE_DIR "${INSTALL_DIR}/include")
set(NCURSES_LIB_DIR     "${INSTALL_DIR}/lib")
file(MAKE_DIRECTORY "${NCURSES_INCLUDE_DIR}" "${NCURSES_LIB_DIR}")

# Export to CMake
add_library(ncurses::ncurses UNKNOWN IMPORTED)
set_target_properties(ncurses::ncurses PROPERTIES
        IMPORTED_LOCATION             "${NCURSES_LIB_DIR}/libncursesw.a" # <= ncurses lib location
        INTERFACE_INCLUDE_DIRECTORIES "${NCURSES_INCLUDE_DIR}"
)
add_dependencies(ncurses::ncurses NCURSES)
include_directories("${NCURSES_INCLUDE_DIR}/ncursesw")
include_directories("${NCURSES_INCLUDE_DIR}")
set(READLINE_MAKE_ENTIRE "${MAKE_EXECUTABLE} ${READLINE_MAKE_ADDITIONAL_FLAGS}")
message(STATUS "READLINE: ${READLINE_MAKE_ENTIRE}")
ExternalProject_Add(READLINE
        PREFIX              ${CMAKE_CURRENT_BINARY_DIR}/readline
        SOURCE_DIR          ${CMAKE_SOURCE_DIR}/src/NonCMake/readline-8.3
        INSTALL_DIR         ${CMAKE_CURRENT_BINARY_DIR}/readline/install
        CONFIGURE_COMMAND   <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> --with-curses ${READLINE_CONFIGURE_ADDITIONAL_FLAGS}
        BUILD_COMMAND       /bin/sh -c "${READLINE_MAKE_ENTIRE}"
        INSTALL_COMMAND     ${MAKE_EXECUTABLE} install ${READLINE_MAKE_INSTALL_ADDITIONAL_FLAGS}
        INSTALL_BYPRODUCTS  <INSTALL_DIR>/lib/libhistory.a <INSTALL_DIR>/lib/libreadline.a <INSTALL_DIR>/lib/libreadline.so <INSTALL_DIR>/lib/libhistory.so
        TEST_COMMAND        ""
        INACTIVITY_TIMEOUT  15
        CONFIGURE_HANDLED_BY_BUILD ON
)

ExternalProject_Get_Property(READLINE INSTALL_DIR)
set(READLINE_INCLUDE_DIR "${INSTALL_DIR}/include")
set(READLINE_LIB_DIR     "${INSTALL_DIR}/lib")
file(MAKE_DIRECTORY "${READLINE_INCLUDE_DIR}" "${READLINE_LIB_DIR}")

# Export to CMake
add_library(readline::readline UNKNOWN IMPORTED)
set_target_properties(readline::readline PROPERTIES
        IMPORTED_LOCATION               "${READLINE_LIB_DIR}/libhistory.a"
        IMPORTED_LOCATION               "${READLINE_LIB_DIR}/libreadline.a"
        INTERFACE_INCLUDE_DIRECTORIES   "${READLINE_INCLUDE_DIR}"
)
add_dependencies(readline::readline READLINE)
add_dependencies(READLINE NCURSES)
include_directories("${READLINE_INCLUDE_DIR}/readline")
include_directories("${READLINE_INCLUDE_DIR}")

include_directories(src/include)
if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    add_compile_definitions(__DEBUG__=1)
endif ()

add_compile_definitions(
        CFS_IMPLEMENT_VERSION="${CFS_IMPLEMENT_VERSION}"
        CFS_STANDARD_VERSION="${CFS_STANDARD_VERSION}"
)

file(TOUCH "${CMAKE_BINARY_DIR}/version.cpp")

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
target_link_libraries(cfs_obj PUBLIC atomic)

add_executable(cfs src/main.cpp ${CMAKE_BINARY_DIR}/version.cpp
        src/include/routes.h src/bin/fsck.cpp src/bin/mkfs.cpp src/bin/mount.cpp)
target_link_libraries(cfs PRIVATE cfs_obj ncurses::ncurses readline::readline atomic)

function(add_link LINK)
    add_custom_target(utility_links_${LINK} ALL
            DEPENDS cfs
            COMMAND ${CMAKE_COMMAND} -E create_symlink cfs ${LINK}.cfs
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Creating utility link ${LINK}"
            BYPRODUCTS ${LINK}.cfs
    )
endfunction()

add_link(fsck)
add_link(mkfs)
add_link(mount)

find_program(XXD_EXEC xxd)
add_custom_target(version_cpp ALL
        COMMAND     /bin/sh -c "\"${XXD_EXEC}\" -i -n version_text < \"${CMAKE_SOURCE_DIR}\"/src/res/version.txt > \"${CMAKE_BINARY_DIR}\"/version.cpp"
        COMMENT     "Generate version.cpp..."
        BYPRODUCTS  "${CMAKE_BINARY_DIR}/version.cpp"
        DEPENDS     ${CMAKE_SOURCE_DIR}/src/res/version.txt
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
        set(compile_args
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
        set(link_args
                # -static-libgcc -static-libstdc++
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

        target_compile_options(cfs PUBLIC ${compile_args})
        target_link_options(cfs PUBLIC ${link_args})

        target_compile_options(cfs_obj PUBLIC ${compile_args})
        target_link_options(cfs_obj PUBLIC ${link_args})
    endif ()
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

if("${DEBUG_TIV_SHOW}" STREQUAL "True")
    add_executable(show src/tests/show.cpp)
    target_link_libraries(show PRIVATE tiv)
    add_dependencies(test_all show)
endif ()
