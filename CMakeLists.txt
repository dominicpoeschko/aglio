cmake_minimum_required(VERSION 3.28)

if(TARGET aglio)
    return()
endif()

include(FetchContent)

FetchContent_Declare(
    glaze
    GIT_REPOSITORY https://github.com/stephenberry/glaze.git
    GIT_TAG main
    PATCH_COMMAND git reset --hard HEAD && git clean -fdx && git apply --3way ${CMAKE_CURRENT_LIST_DIR}/glaze.patch
    UPDATE_COMMAND git fetch origin && git reset --hard @{u} && git clean -fdx && git apply --3way
                   ${CMAKE_CURRENT_LIST_DIR}/glaze.patch)

FetchContent_MakeAvailable(glaze)

add_library(aglio INTERFACE)
target_include_directories(aglio INTERFACE src)
target_link_libraries(aglio INTERFACE glaze::glaze)
add_library(aglio::aglio ALIAS aglio)
