cmake_minimum_required(VERSION 3.28)
project(aglio_tests)

include(FetchContent)

FetchContent_Declare(
    cmake_helpers
    GIT_REPOSITORY https://github.com/dominicpoeschko/cmake_helpers.git
    GIT_TAG master)

FetchContent_MakeAvailable(cmake_helpers)

include(${cmake_helpers_SOURCE_DIR}/FindOrFetch.cmake)

find_or_fetch_package(
    Catch2
    GIT_REPOSITORY
    https://github.com/catchorg/Catch2.git
    GIT_TAG
    v3.12.0
    GIT_BRANCH
    devel)

find_or_fetch_package(
    fmt
    GIT_REPOSITORY
    https://github.com/fmtlib/fmt
    GIT_TAG
    12.1.0
    GIT_BRANCH
    master)

include(${cmake_helpers_SOURCE_DIR}/BuildOptions.cmake)

add_subdirectory(../ ${CMAKE_CURRENT_BINARY_DIR}/aglio)

add_executable(test_aglio test.cpp)

set(USE_STD c++23)

foreach(dep_target fmt Catch2 Catch2WithMain)
    if(TARGET ${dep_target})
        get_target_property(_imported ${dep_target} IMPORTED)
        if(NOT _imported)
            target_add_stdlib_flags(${dep_target})
            target_add_stdlib_debug_flags(${dep_target})
        endif()
    endif()
endforeach()

target_add_default_build_options(test_aglio PRIVATE)
target_link_libraries(test_aglio PRIVATE aglio::aglio fmt::fmt Catch2::Catch2WithMain)

enable_testing()
add_test(NAME aglio_tests COMMAND test_aglio)

# Compile-fail tests: verify that types with reference members trigger a static_assert instead of silently serializing
# nothing.
set(COMPILE_FAIL_EXPECTED_MESSAGE "Described type has members but glaze reflects 0")

foreach(fail_test compile_fail_with_ref compile_fail_with_struct_ref)
    add_library(${fail_test} OBJECT EXCLUDE_FROM_ALL ${fail_test}.cpp)
    target_link_libraries(${fail_test} PRIVATE aglio::aglio)
    target_add_default_build_options(${fail_test} PRIVATE)

    add_test(
        NAME ${fail_test}
        COMMAND
            ${CMAKE_COMMAND} -DBINARY_DIR=${CMAKE_BINARY_DIR} -DTARGET=${fail_test}
            -DEXPECTED_MESSAGE=${COMPILE_FAIL_EXPECTED_MESSAGE} -P ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail_test.cmake)
endforeach()
