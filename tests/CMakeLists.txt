include(${PROJECT_SOURCE_DIR}/cmake/Dependencies.cmake)

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

if(LIBRTDI_ENABLE_WARNINGS)
    librtdi_apply_warnings(librtdi_tests)
endif()

if(LIBRTDI_ENABLE_SANITIZERS)
    librtdi_apply_sanitizers(librtdi_tests)
endif()

catch_discover_tests(librtdi_tests)
