cmake_minimum_required(VERSION 3.18)

if(TARGET aglio)
  return()
endif()

include(FetchContent)

FetchContent_Declare(
  glaze
  GIT_REPOSITORY https://github.com/stephenberry/glaze.git
  GIT_TAG main
  GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(glaze)

add_library(aglio INTERFACE)
target_link_libraries(aglio INTERFACE glaze::glaze)
target_include_directories(aglio INTERFACE src)
add_library(aglio::aglio ALIAS aglio)
