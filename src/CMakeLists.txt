add_library(librtdi STATIC
    registry.cpp
    resolver.cpp
    scope.cpp
    validation.cpp
    exceptions.cpp
)

target_include_directories(librtdi PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(librtdi PUBLIC cxx_std_20)
