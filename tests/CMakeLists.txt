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
    v3.15.1
    GIT_BRANCH
    devel)

find_or_fetch_package(
    fmt
    GIT_REPOSITORY
    https://github.com/fmtlib/fmt
    GIT_TAG
    12.2.0
    GIT_BRANCH
    master)

include(${cmake_helpers_SOURCE_DIR}/BuildOptions.cmake)

# Not optional for the suite: the golden strings expect enums by name.
set(AGLIO_USE_ENCHANTUM
    ON
    CACHE BOOL "" FORCE)

add_subdirectory(../ ${CMAKE_CURRENT_BINARY_DIR}/aglio)

# See the ignorelist; harmless when those checks are not enabled.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(aglio_sanitizer_ignorelist -fsanitize-ignorelist=${CMAKE_CURRENT_SOURCE_DIR}/sanitizer_ignorelist.txt)
else()
    set(aglio_sanitizer_ignorelist)
endif()

add_executable(test_aglio test.cpp)
target_compile_options(test_aglio PRIVATE ${aglio_sanitizer_ignorelist})

set(USE_STD c++23)

# INTERFACE libraries are skipped: they compile nothing, and the helpers set non-INTERFACE properties, which is an error
# on those targets.
foreach(dep_target fmt Catch2 Catch2WithMain)
    if(TARGET ${dep_target})
        get_target_property(_imported ${dep_target} IMPORTED)
        get_target_property(_type ${dep_target} TYPE)
        if(NOT _imported AND NOT _type STREQUAL "INTERFACE_LIBRARY")
            target_add_stdlib_flags(${dep_target})
            target_add_stdlib_debug_flags(${dep_target})
            target_add_sanitizer_flags(${dep_target} PUBLIC)
            target_add_optimizer_flags(${dep_target} PUBLIC)
        endif()
    endif()
endforeach()

target_add_default_build_options(test_aglio PRIVATE)
target_link_libraries(test_aglio PRIVATE aglio::aglio fmt::fmt Catch2::Catch2WithMain)

# Catch2's TEST_CASE/SECTION macros expand __COUNTER__, which Clang flags as a C2y extension under -pedantic. The usage
# is in Catch2, not our code, so silence it.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(test_aglio PRIVATE -Wno-c2y-extensions)
endif()

enable_testing()
add_test(NAME aglio_tests COMMAND test_aglio)

# Fuzzing: enabled whenever the compiler ships libFuzzer (clang today, gcc once it supports -fsanitize=fuzzer).
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS -fsanitize=fuzzer-no-link)
set(CMAKE_REQUIRED_LINK_OPTIONS -fsanitize=fuzzer-no-link)
check_cxx_source_compiles("int main() { return 0; }" AGLIO_HAS_LIBFUZZER)
unset(CMAKE_REQUIRED_FLAGS)
unset(CMAKE_REQUIRED_LINK_OPTIONS)

if(NOT AGLIO_HAS_LIBFUZZER)
    message(STATUS "libFuzzer (-fsanitize=fuzzer) not supported by ${CMAKE_CXX_COMPILER_ID}; fuzz target disabled")
elseif("${USE_SANITIZER}" STREQUAL "thread")
    # libFuzzer's crash handling is incompatible with TSan (signal-unsafe malloc aborts at startup), and the packager is
    # single-threaded anyway.
    message(STATUS "fuzz target disabled in thread sanitizer builds")
else()
    add_executable(fuzz_packager fuzz_packager.cpp)
    target_link_libraries(fuzz_packager PRIVATE aglio::aglio)
    target_add_default_build_options(fuzz_packager PRIVATE)
    # -fno-sanitize-recover: without it UBSan findings print and continue, and the fuzzer sails past them.
    target_compile_options(fuzz_packager PRIVATE -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all
                                                 ${aglio_sanitizer_ignorelist})
    target_link_options(fuzz_packager PRIVATE -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)

    add_executable(fuzz_corpus_generator corpus_generator.cpp)
    target_link_libraries(fuzz_corpus_generator PRIVATE aglio::aglio)
    target_add_default_build_options(fuzz_corpus_generator PRIVATE)
    target_compile_options(fuzz_corpus_generator PRIVATE ${aglio_sanitizer_ignorelist})

    add_test(NAME fuzz_corpus_generate COMMAND fuzz_corpus_generator ${CMAKE_CURRENT_BINARY_DIR}/corpus)
    set_tests_properties(fuzz_corpus_generate PROPERTIES FIXTURES_SETUP fuzz_corpus)

    # libFuzzer reads extra corpus directories without writing to them.
    add_test(NAME fuzz_packager_smoke COMMAND fuzz_packager -max_total_time=10 ${CMAKE_CURRENT_BINARY_DIR}/corpus
                                              ${CMAKE_CURRENT_SOURCE_DIR}/corpus_regressions)
    set_tests_properties(fuzz_packager_smoke PROPERTIES FIXTURES_REQUIRED fuzz_corpus)
endif()

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
