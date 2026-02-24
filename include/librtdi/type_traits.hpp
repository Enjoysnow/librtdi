#pragma once

#include <memory>
#include <tuple>
#include <type_traits>
#include <vector>

namespace librtdi {

// ---------------------------------------------------------------
// Core concepts
// ---------------------------------------------------------------

/// TDerived derives from TBase (or TDerived == TBase for self-registration).
template <typename TDerived, typename TBase>
concept derived_from_base = std::is_base_of_v<TBase, TDerived>;

/// T is default-constructible (for zero-dependency registrations).
template <typename T>
concept default_constructible = std::is_default_constructible_v<T>;

// ---------------------------------------------------------------
// Dependency wrapper tag types
// ---------------------------------------------------------------

/// Marks a dependency as transient.  Constructor receives `std::unique_ptr<T>`.
template <typename T>
struct transient { using type = T; };

/// Explicit singleton marker (optional — bare T means the same).
template <typename T>
struct singleton { using type = T; };

/// Marks a dependency as a collection.
///   `collection<T>`            → singleton collection → `std::vector<T*>`
///   `collection<transient<T>>` → transient collection → `std::vector<std::unique_ptr<T>>`
template <typename T>
struct collection { using type = T; };

// ---------------------------------------------------------------
// dep_traits — extract injection metadata from a dep declaration
// ---------------------------------------------------------------

/// Primary: bare `T` → singleton single, inject as `T&`.
template <typename D>
struct dep_traits {
    using interface_type = D;
    using inject_type    = D&;
    static constexpr bool is_collection = false;
    static constexpr bool is_transient  = false;
};

/// `singleton<T>` → same as bare T.
template <typename T>
struct dep_traits<singleton<T>> {
    using interface_type = T;
    using inject_type    = T&;
    static constexpr bool is_collection = false;
    static constexpr bool is_transient  = false;
};

/// `transient<T>` → inject as `std::unique_ptr<T>`.
template <typename T>
struct dep_traits<transient<T>> {
    using interface_type = T;
    using inject_type    = std::unique_ptr<T>;
    static constexpr bool is_collection = false;
    static constexpr bool is_transient  = true;
};

/// `collection<T>` → singleton collection, inject as `std::vector<T*>`.
template <typename T>
struct dep_traits<collection<T>> {
    using interface_type = T;
    using inject_type    = std::vector<T*>;
    static constexpr bool is_collection = true;
    static constexpr bool is_transient  = false;
};

/// `collection<singleton<T>>` → same as `collection<T>` (singleton is the default).
template <typename T>
struct dep_traits<collection<singleton<T>>> {
    using interface_type = T;
    using inject_type    = std::vector<T*>;
    static constexpr bool is_collection = true;
    static constexpr bool is_transient  = false;
};

/// `collection<transient<T>>` → transient collection, inject as `std::vector<std::unique_ptr<T>>`.
template <typename T>
struct dep_traits<collection<transient<T>>> {
    using interface_type = T;
    using inject_type    = std::vector<std::unique_ptr<T>>;
    static constexpr bool is_collection = true;
    static constexpr bool is_transient  = true;
};

/// Helper alias.
template <typename D>
using inject_type_t = typename dep_traits<D>::inject_type;

// ---------------------------------------------------------------
// Constructibility concept
// ---------------------------------------------------------------

/// TImpl must be constructible from the injection types of all declared deps.
template <typename TImpl, typename... Deps>
concept constructible_from_deps =
    std::is_constructible_v<TImpl, inject_type_t<Deps>...>;

// ---------------------------------------------------------------
// Decorator concepts
// ---------------------------------------------------------------

/// TDecorator(std::unique_ptr<TInterface>) constructible.
template <typename TDecorator, typename TInterface>
concept decorator_constructible =
    std::is_constructible_v<TDecorator, std::unique_ptr<TInterface>>;

/// TDecorator(std::unique_ptr<TInterface>, inject_type_t<Extra>...) constructible.
template <typename TDecorator, typename TInterface, typename... TExtra>
concept decorator_constructible_with_deps =
    std::is_constructible_v<TDecorator, std::unique_ptr<TInterface>, inject_type_t<TExtra>...>;

} // namespace librtdi
