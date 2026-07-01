cmake_minimum_required(VERSION 3.28)

if(TARGET aglio)
    return()
endif()

if(NOT TARGET glaze_glaze)
    find_package(glaze QUIET)
    if(NOT glaze_FOUND)
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
            v7.8.3
            GIT_BRANCH
            main
            PATCH_FILE
            ${CMAKE_CURRENT_LIST_DIR}/glaze.patch)
    endif()
endif()

project(aglio VERSION 0.1.1)

add_library(aglio INTERFACE)
target_include_directories(aglio INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
                                           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(aglio INTERFACE glaze::glaze)
add_library(aglio::aglio ALIAS aglio)

# Install/export support so this can be `cmake --install`ed and consumed elsewhere via find_package(aglio). Off by
# default when aglio is added as a subdirectory / FetchContent'd, so it doesn't pollute the parent project's install
# set. Installation requires glaze to be a pre-installed config package.
option(AGLIO_INSTALL "Generate aglio install/export rules" "${PROJECT_IS_TOP_LEVEL}")

if(AGLIO_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(DIRECTORY src/aglio DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(TARGETS aglio EXPORT aglioTargets)
    install(
        EXPORT aglioTargets
        NAMESPACE aglio::
        FILE aglioTargets.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/aglio)

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/aglioConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/aglioConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/aglio)
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/aglioConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY AnyNewerVersion ARCH_INDEPENDENT)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/aglioConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/aglioConfigVersion.cmake
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/aglio)
endif()
