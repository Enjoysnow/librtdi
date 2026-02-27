# Dependencies.cmake
# Fetches external test dependencies via CMake FetchContent.
# Included by tests/CMakeLists.txt; safe to call multiple times (guarded).

# Try system-installed Catch2 first, fallback to FetchContent
find_package(Catch2 3 QUIET)

if(NOT Catch2_FOUND)
    message(STATUS "Catch2 not found via find_package, fetching with FetchContent...")
    include(FetchContent)

    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://gitcode.com/GitHub_Trending/ca/Catch2.git
        GIT_TAG        v3.7.1
    )
    FetchContent_MakeAvailable(Catch2)

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()

include(Catch)
