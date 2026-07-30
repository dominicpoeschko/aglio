cmake_minimum_required(VERSION 3.28)
project(aglio_remote_fmt_tests)

# Standalone on purpose: src/aglio/remote_fmt.hpp compiles to nothing - silently - unless remote_fmt is present, and
# keeping this out of tests/CMakeLists.txt spares the default build string_constant and nlohmann_json.

include(FetchContent)

FetchContent_Declare(
    cmake_helpers
    GIT_REPOSITORY https://github.com/dominicpoeschko/cmake_helpers.git
    GIT_TAG master)

FetchContent_MakeAvailable(cmake_helpers)

include(${cmake_helpers_SOURCE_DIR}/FindOrFetch.cmake)
include(${cmake_helpers_SOURCE_DIR}/BuildOptions.cmake)

# Tracks master because the goldens in ../check_format.hpp encode behaviour newer than any tagged commit; set a SHA to
# freeze one. Scoped here rather than via the global USE_GIT_BRANCH, which would also drag glaze onto its branch where
# glaze.patch may not apply. For an unpushed remote_fmt, pass -DFETCHCONTENT_SOURCE_DIR_REMOTE_FMT=/path/to/remote_fmt.
set(AGLIO_REMOTE_FMT_GIT_TAG
    "master"
    CACHE STRING "remote_fmt branch or commit to test against")

find_or_fetch_package(
    remote_fmt
    GIT_REPOSITORY
    https://github.com/dominicpoeschko/remote_fmt.git
    GIT_TAG
    ${AGLIO_REMOTE_FMT_GIT_TAG}
    GIT_BRANCH
    master)

add_subdirectory(../../ ${CMAKE_CURRENT_BINARY_DIR}/aglio)

# Not warning-clean under -Weverything and not ours to fix.
if(TARGET remote_fmt)
    set_target_properties(remote_fmt PROPERTIES SYSTEM TRUE)
endif()

# Propagate stdlib/sanitizer flags into the fetched dependencies so libc++/debug-ABI builds link. INTERFACE libraries
# are skipped: they compile nothing, and the helpers set non-INTERFACE properties, which is an error on those targets.
foreach(dep_target fmt nlohmann_json)
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

enable_testing()

# Compile-only, no parser and nothing to link, so the printer side is still verified if the parser's deps break.
add_library(remote_fmt_format_strings OBJECT format_string_tests.cpp)
target_link_libraries(remote_fmt_format_strings PRIVATE aglio::aglio remote_fmt::remote_fmt)
target_compile_definitions(remote_fmt_format_strings PRIVATE REMOTE_FMT_USE_CATALOG=false)
target_add_default_build_options(remote_fmt_format_strings PRIVATE)

# End-to-end: print through the real Printer, decode with the real parser, compare the text. The parent include
# directory is what lets this share types.hpp and the goldens with the fmt/format/ostream suites.
add_executable(test_aglio_remote_fmt remote_fmt_tests.cpp)
target_include_directories(test_aglio_remote_fmt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_aglio_remote_fmt PRIVATE aglio::aglio remote_fmt::remote_fmt remote_fmt::parser)
target_compile_definitions(test_aglio_remote_fmt PRIVATE REMOTE_FMT_USE_CATALOG=false)
target_add_default_build_options(test_aglio_remote_fmt PRIVATE)

add_test(NAME aglio_remote_fmt_tests COMMAND test_aglio_remote_fmt)
