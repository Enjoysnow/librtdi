#pragma once

/// @file export.hpp
/// Cross-platform shared-library symbol visibility macro.
///
/// Build-system defines (set automatically by CMake):
///   LIBRTDI_BUILDING  — defined when compiling the librtdi library itself
///   LIBRTDI_STATIC    — define when building/linking librtdi as a static lib

#if defined(LIBRTDI_STATIC)
  #define LIBRTDI_EXPORT
#elif defined(_WIN32) || defined(__CYGWIN__)
  #ifdef LIBRTDI_BUILDING
    #define LIBRTDI_EXPORT __declspec(dllexport)
  #else
    #define LIBRTDI_EXPORT __declspec(dllimport)
  #endif
#elif defined(__GNUC__) || defined(__clang__)
  #define LIBRTDI_EXPORT __attribute__((visibility("default")))
#else
  #define LIBRTDI_EXPORT
#endif
