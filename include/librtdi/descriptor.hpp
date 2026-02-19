#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

namespace librtdi {

class resolver;

enum class lifetime_kind {
    singleton,
    scoped,
    transient
};

constexpr std::string_view to_string(lifetime_kind lt) noexcept {
    constexpr std::string_view names[] = {"singleton", "scoped", "transient"};
    return names[static_cast<int>(lt)];
}

using factory_fn = std::function<std::shared_ptr<void>(resolver&)>;

/// Describes a single component registration: what interface it provides,
/// what lifetime it has, how to construct it, what it depends on, and
/// an optional key for keyed/named registrations.
///
/// This is a runtime-only value type — no templates. It primarily serves
/// internal registry/resolver bookkeeping, and is exposed via descriptors()
/// for diagnostics in tests and troubleshooting.
struct descriptor {
    // Sentinel default: typeid(void) is used because std::type_index has no
    // default constructor and must be initialized with some type. typeid(void)
    // is a well-defined, stable value that no real component will ever be
    // registered under, making it detectable as "uninitialized" in assertions
    // or diagnostics. Descriptors are only ever constructed internally by
    // registry::register_component, so a default-constructed descriptor exposed
    // to user code would indicate an internal bug.
    std::type_index              component_type = std::type_index(typeid(void));
    lifetime_kind                lifetime       = lifetime_kind::transient;
    factory_fn                   factory;
    std::vector<std::type_index> dependencies;
    std::string                  key;              // empty = non-keyed
    bool                         is_single_slot = false; // true = owning (type,key) slot is locked by Single semantics
    std::optional<std::type_index> impl_type;        // concrete implementation type (for decorator targeting)
    std::optional<std::type_index> forward_target;   // non-empty → this is a forward registration

    /// forward cast hook used by forward expansion (void* -> target -> interface).
    /// Preserved on descriptor for runtime forwarding and diagnostics.
    using forward_cast_fn = std::function<std::shared_ptr<void>(std::shared_ptr<void>)>;
    forward_cast_fn forward_cast;
};

} // namespace librtdi
