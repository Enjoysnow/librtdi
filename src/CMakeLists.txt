add_library(librtdi STATIC
    registry.cpp
    resolver.cpp
    validation.cpp
    exceptions.cpp
)

target_include_directories(librtdi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(librtdi PUBLIC cxx_std_20)

set_target_properties(librtdi PROPERTIES
    VERSION ${PROJECT_VERSION}
    OUTPUT_NAME "rtdi"
)

if(LIBRTDI_ENABLE_WARNINGS)
    librtdi_apply_warnings(librtdi)
endif()

if(LIBRTDI_ENABLE_SANITIZERS)
    librtdi_apply_sanitizers(librtdi)
endif()

install(
    TARGETS librtdi
    EXPORT  librtdi-targets
    ARCHIVE DESTINATION lib
)

install(
    EXPORT librtdi-targets
    DESTINATION lib/cmake/librtdi
    NAMESPACE librtdi::
)
