add_library(librtdi SHARED
    registry.cpp
    resolver.cpp
    validation.cpp
    exceptions.cpp
    stacktrace_capture.cpp
)

target_include_directories(librtdi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(librtdi PUBLIC cxx_std_20)

# Define LIBRTDI_BUILDING when compiling the library itself so that
# export.hpp resolves LIBRTDI_EXPORT to dllexport / visibility("default").
target_compile_definitions(librtdi PRIVATE LIBRTDI_BUILDING)

# -----------------------------------------------------------------------
# Optional: Boost.Stacktrace for registration call-stack capture
# -----------------------------------------------------------------------
if(LIBRTDI_ENABLE_STACKTRACE)
    cmake_policy(SET CMP0167 OLD)
    find_package(Boost 1.65 QUIET COMPONENTS stacktrace_backtrace)
    if(Boost_stacktrace_backtrace_FOUND)
        target_link_libraries(librtdi PRIVATE Boost::stacktrace_backtrace)
        target_compile_definitions(librtdi
            PRIVATE  LIBRTDI_HAS_STACKTRACE
            PUBLIC   LIBRTDI_STACKTRACE_AVAILABLE
        )
        message(STATUS "librtdi: Boost.Stacktrace enabled (backtrace backend)")
    else()
        # Fall back to basic backend
        find_package(Boost 1.65 QUIET COMPONENTS stacktrace_basic)
        if(Boost_stacktrace_basic_FOUND)
            target_link_libraries(librtdi PRIVATE Boost::stacktrace_basic)
            target_compile_definitions(librtdi
                PRIVATE  LIBRTDI_HAS_STACKTRACE
                PUBLIC   LIBRTDI_STACKTRACE_AVAILABLE
            )
            message(STATUS "librtdi: Boost.Stacktrace enabled (basic backend)")
        else()
            message(WARNING "librtdi: Boost.Stacktrace not found — stacktrace capture disabled")
        endif()
    endif()
endif()

set_target_properties(librtdi PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME "rtdi"
    # Hide all symbols by default; only LIBRTDI_EXPORT symbols are visible.
    CXX_VISIBILITY_PRESET     hidden
    VISIBILITY_INLINES_HIDDEN ON
    # Windows: place the DLL next to the test executable so PATH/RPATH
    # issues are avoided when CTest runs from the build tree.
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

# macOS: set install name so consumers can find the dylib after install.
if(APPLE)
    set_target_properties(librtdi PROPERTIES
        INSTALL_RPATH "@loader_path"
        MACOSX_RPATH ON
    )
endif()

if(LIBRTDI_ENABLE_WARNINGS)
    librtdi_apply_warnings(librtdi)
endif()

if(LIBRTDI_ENABLE_SANITIZERS)
    librtdi_apply_sanitizers(librtdi)
endif()

if(LIBRTDI_ENABLE_INSTALL)
    install(
        TARGETS librtdi
        EXPORT  librtdi-targets
        LIBRARY DESTINATION lib          # .so / .dylib
        ARCHIVE DESTINATION lib          # .lib (import lib on Windows)
        RUNTIME DESTINATION bin          # .dll on Windows
    )

    install(
        EXPORT librtdi-targets
        DESTINATION lib/cmake/librtdi
        NAMESPACE librtdi::
    )
endif()