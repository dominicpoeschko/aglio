cmake_minimum_required(VERSION 3.11)

add_library(aglio INTERFACE)
target_include_directories(aglio INTERFACE src)
add_library(aglio::aglio ALIAS aglio)

