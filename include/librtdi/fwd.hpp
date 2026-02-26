#pragma once

/// @file fwd.hpp
/// Forward declarations for all public librtdi symbols.
/// Include this header when you only need to name a type (pointers,
/// references, function parameters) without requiring its full definition.

#include "export.hpp"

#include <cstddef>

namespace librtdi {

// lifetime.hpp
enum class lifetime_kind;

// erased_ptr.hpp
struct erased_ptr;

// decorated_ptr.hpp
template <typename I>
struct decorated_ptr;

// descriptor.hpp
struct build_options;
struct dependency_info;
struct descriptor;

// exceptions.hpp
class di_error;
class not_found;
class cyclic_dependency;
class lifetime_mismatch;
class duplicate_registration;
class resolution_error;

// resolver.hpp
class resolver;

// registry.hpp
template <typename... Deps>
struct deps_tag;
class registry;

} // namespace librtdi
