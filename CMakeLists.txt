cmake_minimum_required(VERSION 3.20)
project(librtdi
    VERSION 0.1.1
    DESCRIPTION "C++20 Runtime Dependency Injection Framework"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Pull in helper functions (compiler warnings, sanitizers)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

option(BUILD_TESTS              "Build unit tests"                                         ON)
option(LIBRTDI_BUILD_EXAMPLES   "Build example programs"                                   OFF)
option(LIBRTDI_ENABLE_WARNINGS  "Enable strict compiler warnings"                          ON)
option(LIBRTDI_ENABLE_SANITIZERS "Enable ASan + UBSan (GCC/Clang) / ASan (MSVC)"           OFF)
option(LIBRTDI_ENABLE_INSTALL   "Enable installation rules"                                ON)
option(LIBRTDI_ENABLE_PACKAGE   "Enable CPack packaging"                                   ON)

add_subdirectory(src)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(LIBRTDI_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# -----------------------------------------------------------------------
# Install rules
# -----------------------------------------------------------------------
if(LIBRTDI_ENABLE_INSTALL)
    install(
        DIRECTORY include/
        DESTINATION include
        FILES_MATCHING PATTERN "*.hpp"
    )

    # Generate CMake package config files for find_package(librtdi)
    include(CMakePackageConfigHelpers)

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/librtdiConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/librtdiConfig.cmake"
        INSTALL_DESTINATION lib/cmake/librtdi
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/librtdiConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/librtdiConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/librtdiConfigVersion.cmake"
        DESTINATION lib/cmake/librtdi
    )
endif()

# -----------------------------------------------------------------------
# CPack packaging
# -----------------------------------------------------------------------
if(LIBRTDI_ENABLE_PACKAGE)
    set(CPACK_PACKAGE_NAME              "librtdi")
    set(CPACK_PACKAGE_VERSION           ${PROJECT_VERSION})
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
    set(CPACK_PACKAGE_CONTACT           "librtdi contributors")

    if(WIN32)
        set(CPACK_GENERATOR "ZIP")
    elseif(APPLE)
        set(CPACK_GENERATOR "TBZ2")
    else()
        set(CPACK_GENERATOR "TGZ;DEB")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
        set(CPACK_DEBIAN_PACKAGE_SECTION      "devel")
    endif()

    include(CPack)
endif()