include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

set(TEST_SOURCES
    test_registration.cpp
    test_resolution.cpp
    test_lifetime.cpp
    test_scope.cpp
    test_multi_impl.cpp
    test_validation.cpp
    test_diagnostics.cpp
    test_concurrency.cpp
    test_auto_wiring.cpp
    test_edge_cases.cpp
    test_keyed.cpp
    test_policy.cpp
    test_forward.cpp
    test_decorator.cpp
)

add_executable(librtdi_tests ${TEST_SOURCES})
target_link_libraries(librtdi_tests PRIVATE librtdi Catch2::Catch2WithMain)

catch_discover_tests(librtdi_tests)
