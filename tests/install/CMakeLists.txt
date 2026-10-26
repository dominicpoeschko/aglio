cmake_minimum_required(VERSION 3.28)
project(aglio_install_consumer CXX)

# Deliberately minimal and dependency-free beyond find_package: this is the consumer's point of view, so it must not
# add_subdirectory aglio or know anything about how it was built.
find_package(aglio CONFIG REQUIRED)

# The exported target must name its own include directory: without it a consumer finds aglio's headers only where the
# compiler searches anyway (/usr/local/include), and silently takes whatever aglio sits there. Read from the targets
# file itself - the config may append enchantum's directory, which can be the same prefix.
file(READ "${aglio_DIR}/aglioTargets.cmake" aglio_targets_file)
if(NOT aglio_targets_file MATCHES "INTERFACE_INCLUDE_DIRECTORIES \"[$]{_IMPORT_PREFIX}/")
    message(FATAL_ERROR "${aglio_DIR}/aglioTargets.cmake exports no include directory for aglio::aglio")
endif()

add_executable(consumer main.cpp)
target_compile_features(consumer PRIVATE cxx_std_23)
target_link_libraries(consumer PRIVATE aglio::aglio)

enable_testing()
add_test(NAME aglio_install_consumer COMMAND consumer)
