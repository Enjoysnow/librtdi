#pragma once

#include "fwd.hpp"
#include "lifetime.hpp"
#include "erased_ptr.hpp"

#include <any>
#include <functional>
#include <optional>
#include <source_location>
#include <string>
#include <typeindex>
#include <vector>

namespace librtdi {

using factory_fn = std::function<erased_ptr(resolver&)>;

using forward_cast_fn = std::function<void*(void*)>;

// ---------------------------------------------------------------
// build_options — controls build-time behaviour
// ---------------------------------------------------------------

struct build_options {
    bool validate_on_build        = true;
    bool validate_lifetimes       = true;
    bool detect_cycles            = true;
    bool eager_singletons         = true;
    bool allow_empty_collections  = true;
};

// ---------------------------------------------------------------
// dependency_info — metadata for a single declared dependency
// ---------------------------------------------------------------

struct dependency_info {
    std::type_index type;
    bool is_collection = false;
    bool is_transient  = false;

    bool operator==(const dependency_info&) const = default;
};

// ---------------------------------------------------------------
// descriptor — one component registration record
// ---------------------------------------------------------------

struct descriptor {
    std::type_index component_type = std::type_index(typeid(void));
    lifetime_kind   lifetime       = lifetime_kind::transient;
    factory_fn      factory;
    std::vector<dependency_info> dependencies;
    std::string     key;               // empty = non-keyed
    bool            is_collection = false;
    std::optional<std::type_index> impl_type;

    // Forward registration support
    std::optional<std::type_index> forward_target;

    forward_cast_fn forward_cast;

    /// Source location of the user code that registered this descriptor.
    std::source_location registration_location{};

    /// Full call stack captured when the descriptor was registered.
    /// Contains boost::stacktrace::stacktrace when LIBRTDI_HAS_STACKTRACE is
    /// defined; empty std::any otherwise. Accessed via internal helpers only.
    std::any registration_stacktrace;
};

} // namespace librtdi
