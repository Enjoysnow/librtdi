#pragma once

#include "export.hpp"
#include "decorated_ptr.hpp"
#include "descriptor.hpp"
#include "resolver.hpp"
#include "exceptions.hpp"
#include "type_traits.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>
#include <typeindex>
#include <type_traits>
#include <utility>
#include <vector>

namespace librtdi {

/// A zero-size tag type that carries a compile-time dependency type list.
template <typename... Deps>
struct deps_tag {
    using type_list = std::tuple<Deps...>;
    static constexpr std::size_t count = sizeof...(Deps);
};

template <typename... Deps>
inline constexpr deps_tag<Deps...> deps{};

// ---------------------------------------------------------------
// Helper: resolve a single dep at factory-call time
// ---------------------------------------------------------------
namespace detail {

template <typename D>
auto resolve_dep(resolver& r) -> inject_type_t<D> {
    using traits = dep_traits<D>;
    using I = typename traits::interface_type;

    if constexpr (traits::is_collection && traits::is_transient) {
        // collection<transient<T>> → vector<unique_ptr<T>>
        return r.create_all<I>();
    } else if constexpr (traits::is_collection && !traits::is_transient) {
        // collection<T> → vector<T*>
        return r.get_all<I>();
    } else if constexpr (!traits::is_collection && traits::is_transient) {
        // transient<T> → unique_ptr<T>
        return r.create<I>();
    } else {
        // bare T / singleton<T> → T&
        return r.get<I>();
    }
}

/// Build a vector<dependency_info> from deps type list.
template <typename... Deps>
std::vector<dependency_info> make_dep_infos() {
    return { dependency_info{
        std::type_index(typeid(typename dep_traits<Deps>::interface_type)),
        dep_traits<Deps>::is_collection,
        dep_traits<Deps>::is_transient
    }... };
}

} // namespace detail

// ---------------------------------------------------------------
// registry
// ---------------------------------------------------------------

class LIBRTDI_EXPORT registry {
public:
    registry();
    ~registry();

    registry(const registry&) = delete;
    registry& operator=(const registry&) = delete;
    registry(registry&&) noexcept;
    registry& operator=(registry&&) noexcept;

    // ===============================================================
    // Singleton single-instance registration
    // ===============================================================

    /// Zero-dep singleton
    template <typename TInterface, typename TImpl>
        requires derived_from_base<TImpl, TInterface>
              && default_constructible<TImpl>
    registry& add_singleton(std::source_location loc = std::source_location::current()) {
        static_assert(std::is_same_v<TInterface, TImpl>
                   || std::has_virtual_destructor_v<TInterface>,
            "add_singleton<I,T>: I must have a virtual destructor when I != T");
        return register_single(
            typeid(TInterface), lifetime_kind::singleton,
            [](resolver&) -> erased_ptr { return make_erased_as<TInterface, TImpl>(); },
            {}, {}, std::type_index(typeid(TImpl)), loc);
    }

    /// Singleton with deps
    template <typename TInterface, typename TImpl, typename... Deps>
        requires derived_from_base<TImpl, TInterface>
              && constructible_from_deps<TImpl, Deps...>
    registry& add_singleton(deps_tag<Deps...>, std::source_location loc = std::source_location::current()) {
        static_assert(std::is_same_v<TInterface, TImpl>
                   || std::has_virtual_destructor_v<TInterface>,
            "add_singleton<I,T>: I must have a virtual destructor when I != T");
        return register_single(
            typeid(TInterface), lifetime_kind::singleton,
            [](resolver& r) -> erased_ptr {
                return make_erased_as<TInterface, TImpl>(detail::resolve_dep<Deps>(r)...);
            },
            detail::make_dep_infos<Deps...>(), {},
            std::type_index(typeid(TImpl)), loc);
    }

    /// Keyed zero-dep singleton
    template <typename TInterface, typename TImpl>
        requires derived_from_base<TImpl, TInterface>
              && default_constructible<TImpl>
    registry& add_singleton(std::string_view key, std::source_location loc = std::source_location::current()) {
        static_assert(std::is_same_v<TInterface, TImpl>
                   || std::has_virtual_destructor_v<TInterface>,
            "add_singleton<I,T>: I must have a virtual destructor when I != T");
        return register_single(
            typeid(TInterface), lifetime_kind::singleton,
            [](resolver&) -> erased_ptr { return make_erased_as<TInterface, TImpl>(); },
            {}, std::string(key), std::type_index(typeid(TImpl)), loc);
    }

    /// Keyed singleton with deps
    template <typename TInterface, typename TImpl, typename... Deps>
        requires derived_from_base<TImpl, TInterface>
              && constructible_from_deps<TImpl, Deps...>
    registry& add_singleton(std::string_view key, deps_tag<Deps...>, std::source_location loc = std::source_location::current()) {
        static_assert(std::is_same_v<TInterface, TImpl>
                   || std::has_virtual_destructor_v<TInterface>,
            "add_singleton<I,T>: I must have a virtual destructor when I != T");
        return register_single(
            typeid(TInterface), lifetime_kind::singleton,
            [](resolver& r) -> erased_ptr {
                return make_erased_as<TInterface, TImpl>(detail::resolve_dep<Deps>(r)...);
            },
            detail::make_dep_infos<Deps...>(), std::string(key),
            std::type_index(typeid(TImpl)), loc);
    }

    // ===============================================================
    // Transient single-instance registration
    // ===============================================================

    template <typename TInterface, typename TImpl>
        requires derived_from_base<TImpl, TInterface>
              && default_constructible<TImpl>
    registry& add_transient(std::source_location loc = std::source_location::current()) {
        static_assert(std::is_same_v<TInterface, TImpl>
                   || std::has_virtual_destructor_v<TInterface>,
            "add_transient<I,T>: I must have a virtual destructor when I != T");
        return register_single(
            typeid(TInterface), lifetime_kind::transient,
            [](resolver&) -> erased_ptr { return make_erased_as<TInterface, TImpl>(); },
            {}, {}, std::type_index(typeid(TImpl)), loc);
    }

    template <typename TInterface, typename TImpl, typename... Deps>
        requires derived_from_base<TImpl, TInterface>
              && constructible_from_deps<TImpl, Deps...>
    registry& add_transient(deps_tag<Deps...>, std::source_location loc = std::source_location::current()) {
        static_assert(std::is_same_v<TInterface, TImpl>
                   || std::has_virtual_destructor_v<TInterface>,
            "add_transient<I,T>: I must have a virtual destructor when I != T");
        return register_single(
            typeid(TInterface), lifetime_kind::transient,
            [](resolver& r) -> erased_ptr {
                return make_erased_as<TInterface, TImpl>(detail::resolve_dep<Deps>(r)...);
            },
            detail::make_dep_infos<Deps...>(), {},
            std::type_index(typeid(TImpl)), loc);
    }

    template <typename TInterface, typename TImpl>
        requires derived_from_base<TImpl, TInterface>
              && default_constructible<TImpl>
    registry& add_transient(std::string_view key, std::source_location loc = std::source_location::current()) {
        static_assert(std::is_same_v<TInterface, TImpl>
                   || std::has_virtual_destructor_v<TInterface>,
            "add_transient<I,T>: I must have a virtual destructor when I != T");
        return register_single(
            typeid(TInterface), lifetime_kind::transient,
            [](resolver&) -> erased_ptr { return make_erased_as<TInterface, TImpl>(); },
            {}, std::string(key), std::type_index(typeid(TImpl)), loc);
    }

    template <typename TInterface, typename TImpl, typename... Deps>
        requires derived_from_base<TImpl, TInterface>
              && constructible_from_deps<TImpl, Deps...>
    registry& add_transient(std::string_view key, deps_tag<Deps...>, std::source_location loc = std::source_location::current()) {
        static_assert(std::is_same_v<TInterface, TImpl>
                   || std::has_virtual_destructor_v<TInterface>,
            "add_transient<I,T>: I must have a virtual destructor when I != T");
        return register_single(
            typeid(TInterface), lifetime_kind::transient,
            [](resolver& r) -> erased_ptr {
                return make_erased_as<TInterface, TImpl>(detail::resolve_dep<Deps>(r)...);
            },
            detail::make_dep_infos<Deps...>(), std::string(key),
            std::type_index(typeid(TImpl)), loc);
    }

    // ===============================================================
    // Collection registration (multiple impls per interface, freely append)
    // ===============================================================

    template <typename TInterface, typename TImpl>
        requires derived_from_base<TImpl, TInterface>
              && default_constructible<TImpl>
    registry& add_collection(lifetime_kind lifetime, std::source_location loc = std::source_location::current()) {
        static_assert(std::is_same_v<TInterface, TImpl>
                   || std::has_virtual_destructor_v<TInterface>,
            "add_collection<I,T>: I must have a virtual destructor when I != T");
        return register_collection(
            typeid(TInterface), lifetime,
            [](resolver&) -> erased_ptr { return make_erased_as<TInterface, TImpl>(); },
            {}, {}, std::type_index(typeid(TImpl)), loc);
    }

    template <typename TInterface, typename TImpl, typename... Deps>
        requires derived_from_base<TImpl, TInterface>
              && constructible_from_deps<TImpl, Deps...>
    registry& add_collection(lifetime_kind lifetime, deps_tag<Deps...>, std::source_location loc = std::source_location::current()) {
        static_assert(std::is_same_v<TInterface, TImpl>
                   || std::has_virtual_destructor_v<TInterface>,
            "add_collection<I,T>: I must have a virtual destructor when I != T");
        return register_collection(
            typeid(TInterface), lifetime,
            [](resolver& r) -> erased_ptr {
                return make_erased_as<TInterface, TImpl>(detail::resolve_dep<Deps>(r)...);
            },
            detail::make_dep_infos<Deps...>(), {},
            std::type_index(typeid(TImpl)), loc);
    }

    template <typename TInterface, typename TImpl>
        requires derived_from_base<TImpl, TInterface>
              && default_constructible<TImpl>
    registry& add_collection(std::string_view key, lifetime_kind lifetime, std::source_location loc = std::source_location::current()) {
        static_assert(std::is_same_v<TInterface, TImpl>
                   || std::has_virtual_destructor_v<TInterface>,
            "add_collection<I,T>: I must have a virtual destructor when I != T");
        return register_collection(
            typeid(TInterface), lifetime,
            [](resolver&) -> erased_ptr { return make_erased_as<TInterface, TImpl>(); },
            {}, std::string(key), std::type_index(typeid(TImpl)), loc);
    }

    template <typename TInterface, typename TImpl, typename... Deps>
        requires derived_from_base<TImpl, TInterface>
              && constructible_from_deps<TImpl, Deps...>
    registry& add_collection(std::string_view key, lifetime_kind lifetime, deps_tag<Deps...>, std::source_location loc = std::source_location::current()) {
        static_assert(std::is_same_v<TInterface, TImpl>
                   || std::has_virtual_destructor_v<TInterface>,
            "add_collection<I,T>: I must have a virtual destructor when I != T");
        return register_collection(
            typeid(TInterface), lifetime,
            [](resolver& r) -> erased_ptr {
                return make_erased_as<TInterface, TImpl>(detail::resolve_dep<Deps>(r)...);
            },
            detail::make_dep_infos<Deps...>(), std::string(key),
            std::type_index(typeid(TImpl)), loc);
    }

    // ===============================================================
    // Forward registration
    // ===============================================================

    /// Forward all registrations (all 4 slots) of TTarget to TInterface.
    template <typename TInterface, typename TTarget>
        requires derived_from_base<TTarget, TInterface>
    registry& forward(std::source_location loc = std::source_location::current()) {
        static_assert(std::has_virtual_destructor_v<TInterface>,
            "forward<I,T>: I must have a virtual destructor for forward registration");
        return register_forward(
            typeid(TInterface),
            std::type_index(typeid(TTarget)),
            [](void* raw) -> void* {
                return static_cast<TInterface*>(static_cast<TTarget*>(raw));
            },
            [](void* p) { delete static_cast<TInterface*>(p); },
            loc);
    }

    // ===============================================================
    // Decorator registration
    // ===============================================================

    /// Decorate all registrations of I with D (no extra deps).
    template <typename TInterface, typename TDecorator>
        requires derived_from_base<TDecorator, TInterface>
              && decorator_constructible<TDecorator, TInterface>
    registry& decorate(std::source_location loc = std::source_location::current()) {
        static_assert(std::has_virtual_destructor_v<TInterface>,
            "decorate<I,D>: I must have a virtual destructor for decorator registration");
        return register_decorator(
            typeid(TInterface), std::nullopt,
            [](factory_fn inner) -> factory_fn {
                return [inner = std::move(inner)](resolver& r) -> erased_ptr {
                    auto ep = inner(r);
                    auto* typed = static_cast<TInterface*>(ep.get());
                    decorated_ptr<TInterface> handle(typed, std::move(ep));
                    return make_erased_as<TInterface, TDecorator>(std::move(handle));
                };
            },
            {}, loc);
    }

    /// Decorate all of I with D, with extra deps.
    template <typename TInterface, typename TDecorator, typename... Extra>
        requires derived_from_base<TDecorator, TInterface>
              && decorator_constructible_with_deps<TDecorator, TInterface, Extra...>
    registry& decorate(deps_tag<Extra...>, std::source_location loc = std::source_location::current()) {
        static_assert(std::has_virtual_destructor_v<TInterface>,
            "decorate<I,D>: I must have a virtual destructor for decorator registration");
        return register_decorator(
            typeid(TInterface), std::nullopt,
            [](factory_fn inner) -> factory_fn {
                return [inner = std::move(inner)](resolver& r) -> erased_ptr {
                    auto ep = inner(r);
                    auto* typed = static_cast<TInterface*>(ep.get());
                    decorated_ptr<TInterface> handle(typed, std::move(ep));
                    return make_erased_as<TInterface, TDecorator>(
                        std::move(handle),
                        detail::resolve_dep<Extra>(r)...);
                };
            },
            detail::make_dep_infos<Extra...>(), loc);
    }

    /// Decorate a specific impl of I with D (no extra deps).
    template <typename TInterface, typename TDecorator>
        requires derived_from_base<TDecorator, TInterface>
              && decorator_constructible<TDecorator, TInterface>
    registry& decorate(std::type_index target, std::source_location loc = std::source_location::current()) {
        static_assert(std::has_virtual_destructor_v<TInterface>,
            "decorate<I,D>: I must have a virtual destructor for decorator registration");
        return register_decorator(
            typeid(TInterface), target,
            [](factory_fn inner) -> factory_fn {
                return [inner = std::move(inner)](resolver& r) -> erased_ptr {
                    auto ep = inner(r);
                    auto* typed = static_cast<TInterface*>(ep.get());
                    decorated_ptr<TInterface> handle(typed, std::move(ep));
                    return make_erased_as<TInterface, TDecorator>(std::move(handle));
                };
            },
            {}, loc);
    }

    /// Decorate a specific impl of I with D, with extra deps.
    template <typename TInterface, typename TDecorator, typename... Extra>
        requires derived_from_base<TDecorator, TInterface>
              && decorator_constructible_with_deps<TDecorator, TInterface, Extra...>
    registry& decorate(std::type_index target, deps_tag<Extra...>, std::source_location loc = std::source_location::current()) {
        static_assert(std::has_virtual_destructor_v<TInterface>,
            "decorate<I,D>: I must have a virtual destructor for decorator registration");
        return register_decorator(
            typeid(TInterface), target,
            [](factory_fn inner) -> factory_fn {
                return [inner = std::move(inner)](resolver& r) -> erased_ptr {
                    auto ep = inner(r);
                    auto* typed = static_cast<TInterface*>(ep.get());
                    decorated_ptr<TInterface> handle(typed, std::move(ep));
                    return make_erased_as<TInterface, TDecorator>(
                        std::move(handle),
                        detail::resolve_dep<Extra>(r)...);
                };
            },
            detail::make_dep_infos<Extra...>(), loc);
    }

    /// Decorate a specific impl TTarget of I with D (type-safe target, no extra deps).
    /// Usage: registry.decorate<IFoo, FooDecorator, FooImpl>()
    template <typename TInterface, typename TDecorator, typename TTarget>
        requires derived_from_base<TDecorator, TInterface>
              && decorator_constructible<TDecorator, TInterface>
              && derived_from_base<TTarget, TInterface>
    registry& decorate_target(std::source_location loc = std::source_location::current()) {
        static_assert(std::has_virtual_destructor_v<TInterface>,
            "decorate_target<I,D,T>: I must have a virtual destructor for decorator registration");
        return register_decorator(
            typeid(TInterface), std::type_index(typeid(TTarget)),
            [](factory_fn inner) -> factory_fn {
                return [inner = std::move(inner)](resolver& r) -> erased_ptr {
                    auto ep = inner(r);
                    auto* typed = static_cast<TInterface*>(ep.get());
                    decorated_ptr<TInterface> handle(typed, std::move(ep));
                    return make_erased_as<TInterface, TDecorator>(std::move(handle));
                };
            },
            {}, loc);
    }

    /// Decorate a specific impl TTarget of I with D (type-safe target, with extra deps).
    /// Usage: registry.decorate_target<IFoo, FooDecorator, FooImpl>(deps<IBar>)
    template <typename TInterface, typename TDecorator, typename TTarget, typename... Extra>
        requires derived_from_base<TDecorator, TInterface>
              && decorator_constructible_with_deps<TDecorator, TInterface, Extra...>
              && derived_from_base<TTarget, TInterface>
    registry& decorate_target(deps_tag<Extra...>, std::source_location loc = std::source_location::current()) {
        static_assert(std::has_virtual_destructor_v<TInterface>,
            "decorate_target<I,D,T>: I must have a virtual destructor for decorator registration");
        return register_decorator(
            typeid(TInterface), std::type_index(typeid(TTarget)),
            [](factory_fn inner) -> factory_fn {
                return [inner = std::move(inner)](resolver& r) -> erased_ptr {
                    auto ep = inner(r);
                    auto* typed = static_cast<TInterface*>(ep.get());
                    decorated_ptr<TInterface> handle(typed, std::move(ep));
                    return make_erased_as<TInterface, TDecorator>(
                        std::move(handle),
                        detail::resolve_dep<Extra>(r)...);
                };
            },
            detail::make_dep_infos<Extra...>(), loc);
    }

    // ===============================================================
    // Build
    // ===============================================================

    std::shared_ptr<resolver> build(build_options options = {},
                                     std::source_location loc = std::source_location::current());

    const std::vector<descriptor>& descriptors() const;

private:
    // Single-instance slot registration (0..1 per (type, key, lifetime))
    registry& register_single(std::type_index type, lifetime_kind lifetime,
                              factory_fn factory,
                              std::vector<dependency_info> deps,
                              std::string key,
                              std::optional<std::type_index> impl_type,
                              std::source_location loc);

    // Collection slot registration (0..N per (type, key, lifetime))
    registry& register_collection(std::type_index type, lifetime_kind lifetime,
                                  factory_fn factory,
                                  std::vector<dependency_info> deps,
                                  std::string key,
                                  std::optional<std::type_index> impl_type,
                                  std::source_location loc);

    // Forward registration
    registry& register_forward(std::type_index interface_type,
                               std::type_index target_type,
                               forward_cast_fn cast,
                               void (*forward_deleter)(void*),
                               std::source_location loc);

    using decorator_wrapper = std::function<factory_fn(factory_fn)>;

    registry& register_decorator(std::type_index interface_type,
                                 std::optional<std::type_index> target_impl,
                                 decorator_wrapper wrapper,
                                 std::vector<dependency_info> extra_deps,
                                 std::source_location loc);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace librtdi
