# CompilerWarnings.cmake
# Provides functions to apply consistent compiler warning flags and sanitizers
# across GCC, Clang, and MSVC (VS2019+). Consumed by src/ and tests/.

# librtdi_apply_warnings(<target>)
# Applies strict warning flags appropriate for the active compiler.
function(librtdi_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4           # Warning level 4
            /permissive-  # Conformance mode
            /utf-8        # Source and execution charset UTF-8
        )
    else()
        # GCC and Clang share the same flag set
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
            -Wconversion
            -Wsign-conversion
            -Wnull-dereference
            -Wdouble-promotion
            -Wformat=2
        )
    endif()
endfunction()

# librtdi_apply_sanitizers(<target>)
# Enables AddressSanitizer + UBSanitizer on GCC/Clang, AddressSanitizer on MSVC.
# Controlled by the LIBRTDI_ENABLE_SANITIZERS option in the root CMakeLists.
function(librtdi_apply_sanitizers target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined
        )
    elseif(MSVC)
        # MSVC ASan is available from VS 16.9 onward
        target_compile_options(${target} PRIVATE /fsanitize=address)
    endif()
endfunction()
