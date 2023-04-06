cmake_minimum_required(VERSION 3.18)

if(TARGET aglio)
  return()
endif()

add_library(aglio INTERFACE)
target_include_directories(aglio INTERFACE src)
add_library(aglio::aglio ALIAS aglio)
