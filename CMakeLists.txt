cmake_minimum_required(VERSION 3.20)
project(librtdi VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS "Build unit tests" ON)
option(LIBRTDI_ENABLE_WARNINGS "Enable strict compiler warnings for librtdi targets" ON)
option(LIBRTDI_ENABLE_SANITIZERS "Enable address/undefined sanitizers for librtdi targets (GCC/Clang)" OFF)

function(librtdi_configure_target target_name)
    if(NOT TARGET ${target_name})
        return()
    endif()

    if(LIBRTDI_ENABLE_WARNINGS)
        if(MSVC)
            target_compile_options(${target_name} PRIVATE /W4 /permissive-)
        else()
            target_compile_options(${target_name} PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wsign-conversion
            )
        endif()
    endif()

    if(LIBRTDI_ENABLE_SANITIZERS)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target_name} PRIVATE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
            )
            target_link_options(${target_name} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()

add_subdirectory(src)
librtdi_configure_target(librtdi)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    librtdi_configure_target(librtdi_tests)
endif()
