cmake_minimum_required(VERSION 3.28)

if(TARGET aglio)
    return()
endif()

include(FetchContent)

FetchContent_Declare(
    cmake_helpers
    GIT_REPOSITORY https://github.com/dominicpoeschko/cmake_helpers.git
    GIT_TAG master)

FetchContent_MakeAvailable(cmake_helpers)

include(${cmake_helpers_SOURCE_DIR}/FindOrFetch.cmake)

find_or_fetch_package(
    glaze
    GIT_REPOSITORY
    https://github.com/stephenberry/glaze.git
    GIT_TAG
    v7.0.1
    GIT_BRANCH
    main
    PATCH_FILE
    ${CMAKE_CURRENT_LIST_DIR}/glaze.patch)

add_library(aglio INTERFACE)
target_include_directories(aglio INTERFACE src)
target_link_libraries(aglio INTERFACE glaze::glaze)
add_library(aglio::aglio ALIAS aglio)
