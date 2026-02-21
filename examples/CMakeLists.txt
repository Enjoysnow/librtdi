cmake_minimum_required(VERSION 3.20)

set(EXAMPLE_SOURCES
    basic_usage.cpp
)

foreach(src ${EXAMPLE_SOURCES})
    get_filename_component(target_name ${src} NAME_WE)
    add_executable(example_${target_name} ${src})
    target_link_libraries(example_${target_name} PRIVATE librtdi)
    target_compile_features(example_${target_name} PRIVATE cxx_std_20)
    if(LIBRTDI_ENABLE_WARNINGS)
        librtdi_apply_warnings(example_${target_name})
    endif()
endforeach()
