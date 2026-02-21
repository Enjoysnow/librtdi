# Dependencies.cmake
# Fetches external test dependencies via CMake FetchContent.
# Included by tests/CMakeLists.txt; safe to call multiple times (guarded).

include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)
