cmake_minimum_required(VERSION 3.28)

if(TARGET aglio)
    return()
endif()

if(NOT TARGET glaze_glaze)
    # USE_FORCE_FETCH (see cmake_helpers/FindOrFetch.cmake) guarantees fetched sources even when the package is
    # installed system-wide - dependency packaging relies on that, so skip the probe.
    if(NOT USE_FORCE_FETCH)
        find_package(glaze QUIET)
    endif()
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
            v9.1.0
            GIT_BRANCH
            main
            PATCH_FILE
            ${CMAKE_CURRENT_LIST_DIR}/glaze.patch)
    endif()
endif()

project(aglio VERSION 0.3.4)

# Before the target: its INSTALL_INTERFACE include directory is CMAKE_INSTALL_INCLUDEDIR, empty until this runs. With it
# later, the exported target carried no include directory, and an installed aglio only worked from a prefix the compiler
# searches anyway (/usr/local) - from any other prefix a consumer silently compiled against whatever aglio sat there.
# `just install-check` checks the exported directory.
include(GNUInstallDirs)

add_library(aglio INTERFACE)
target_include_directories(aglio INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
                                           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(aglio INTERFACE glaze::glaze)
add_library(aglio::aglio ALIAS aglio)

# On serializes mp-units quantities (as their number in their own unit; serializer.hpp) and brings mp-units: the
# parent's mp-units target if it has one, else v2.5.0 fetched. Off: no mp-units anywhere.
option(AGLIO_USE_MP_UNITS "Serialize mp-units quantities" OFF)

if(AGLIO_USE_MP_UNITS)
    if(NOT TARGET mp-units::core)
        include(FetchContent)
        # hosted defaults, no contract library (gsl-lite) and no C++ modules: the headers are all aglio needs
        set(MP_UNITS_API_CONTRACTS
            "NONE"
            CACHE STRING "")
        set(MP_UNITS_BUILD_CXX_MODULES
            OFF
            CACHE BOOL "")
        set(MP_UNITS_BUILD_INSTALL
            OFF
            CACHE BOOL "")
        FetchContent_Declare(
            mp-units
            GIT_REPOSITORY https://github.com/mpusz/mp-units.git
            GIT_TAG v2.5.0
            GIT_SHALLOW ON
            SYSTEM SOURCE_SUBDIR src)
        FetchContent_MakeAvailable(mp-units)
    endif()
    target_link_libraries(aglio INTERFACE mp-units::core)
    target_compile_definitions(aglio INTERFACE AGLIO_USE_MP_UNITS)
endif()

# Off drops the dependency, at the cost of enums not printing at all. Also turn it off if you define your own enum
# formatters: enchantum's match every enum, so a second one for the same type is ambiguous.
option(AGLIO_USE_ENCHANTUM "Print enums by name in the fmt, std::format and ostream output" ON)

if(AGLIO_USE_ENCHANTUM)
    # Located by header, not by find_package: enchantum is header-only, its include directory is all aglio needs, and an
    # installed copy's CMake package is never read (before 0.5.0 it put its config and its targets file in two
    # directories, and config mode was a hard error). aglioConfig.cmake.in does the same for consumers. A parent project
    # that already provides the target wins over both.
    if(NOT TARGET enchantum::enchantum)
        # Skipped under USE_FORCE_FETCH for the same reason as the glaze probe above.
        if(NOT USE_FORCE_FETCH)
            find_path(aglio_enchantum_include_dir enchantum/enchantum.hpp)
            mark_as_advanced(aglio_enchantum_include_dir)
        endif()

        if(NOT aglio_enchantum_include_dir)
            include(FetchContent)

            # Pinned to the version remote_fmt uses, so a project pulling in both gets a single copy.
            FetchContent_Declare(
                enchantum
                GIT_REPOSITORY https://github.com/ZXShady/enchantum
                GIT_TAG 0.5.0
                SYSTEM)

            FetchContent_MakeAvailable(enchantum)
        endif()
    endif()

    if(TARGET enchantum::enchantum)
        target_link_libraries(aglio INTERFACE $<BUILD_INTERFACE:enchantum::enchantum>)
    else()
        target_include_directories(aglio SYSTEM INTERFACE $<BUILD_INTERFACE:${aglio_enchantum_include_dir}>)
    endif()

    target_compile_definitions(aglio INTERFACE AGLIO_USE_ENCHANTUM)
endif()

# Install/export support so this can be `cmake --install`ed and consumed elsewhere via find_package(aglio). Off by
# default when aglio is added as a subdirectory / FetchContent'd, so it doesn't pollute the parent project's install
# set. Installation requires glaze to be a pre-installed config package.
option(AGLIO_INSTALL "Generate aglio install/export rules" "${PROJECT_IS_TOP_LEVEL}")

if(AGLIO_INSTALL)
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
