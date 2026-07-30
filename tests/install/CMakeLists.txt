cmake_minimum_required(VERSION 3.28)
project(aglio_install_consumer CXX)

# Deliberately minimal and dependency-free beyond find_package: this is the consumer's point of view, so it must not
# add_subdirectory aglio or know anything about how it was built.
find_package(aglio CONFIG REQUIRED)

add_executable(consumer main.cpp)
target_compile_features(consumer PRIVATE cxx_std_23)
target_link_libraries(consumer PRIVATE aglio::aglio)

enable_testing()
add_test(NAME aglio_install_consumer COMMAND consumer)
