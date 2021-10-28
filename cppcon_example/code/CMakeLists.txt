cmake_minimum_required(VERSION 3.16)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(demo)

add_executable(demo main.cpp)
target_compile_features(demo PUBLIC cxx_std_20)
target_compile_options(demo PUBLIC
    -fsanitize=address,undefined
    -Weverything
    -Wno-c++98-compat
)

target_link_options(demo PUBLIC
    -fsanitize=address,undefined
)
