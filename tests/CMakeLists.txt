cmake_minimum_required(VERSION 3.28)
project(aglio_tests)

include(FetchContent)

FetchContent_Declare(
    cmake_helpers
    GIT_REPOSITORY https://github.com/dominicpoeschko/cmake_helpers.git
    GIT_TAG master)

FetchContent_MakeAvailable(cmake_helpers)

include(${cmake_helpers_SOURCE_DIR}/FindOrFetch.cmake)
find_or_fetch_package(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.11.0)

find_or_fetch_package(fmt GIT_REPOSITORY https://github.com/fmtlib/fmt GIT_TAG v12.1.0)

include(${cmake_helpers_SOURCE_DIR}/BuildOptions.cmake)

add_subdirectory(../ ${CMAKE_CURRENT_BINARY_DIR}/aglio)

add_executable(test_aglio test.cpp)

target_add_default_build_options(test_aglio PRIVATE)
target_link_libraries(test_aglio PRIVATE aglio::aglio fmt::fmt Catch2::Catch2WithMain)

enable_testing()
add_test(NAME aglio_tests COMMAND test_aglio)
