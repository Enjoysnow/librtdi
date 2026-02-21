#pragma once

#include <memory>
#include <type_traits>

namespace librtdi {

/// TDerived must be derived from TBase, or TDerived == TBase (self-registration).
template <typename TDerived, typename TBase>
concept derived_from_base = std::is_base_of_v<TBase, TDerived>;

/// T must be default-constructible (required for zero-dependency registrations).
template <typename T>
concept default_constructible = std::is_default_constructible_v<T>;

/// T must be constructible from (shared_ptr<D1>, ..., shared_ptr<Dn>).
/// Used by deps<D...>-based registrations to verify the constructor signature
/// matches the declared dependency list at compile time.
template <typename T, typename... Deps>
concept constructible_from_shared_ptrs = std::is_constructible_v<T, std::shared_ptr<Deps>...>;

/// TDecorator must be constructible from shared_ptr<TInterface>.
/// Used by the no-extra-deps forms of decorate<I, D>().
template <typename TDecorator, typename TInterface>
concept decorator_constructible =
    std::is_constructible_v<TDecorator, std::shared_ptr<TInterface>>;

/// TDecorator must be constructible from (shared_ptr<TInterface>, shared_ptr<TExtra>...).
/// Used by the deps<>-carrying forms of decorate<I, D>(deps<Extra...>).
template <typename TDecorator, typename TInterface, typename... TExtra>
concept decorator_constructible_with_deps =
    std::is_constructible_v<TDecorator, std::shared_ptr<TInterface>, std::shared_ptr<TExtra>...>;

} // namespace librtdi
