#pragma once

#include "descriptor.hpp"
#include "resolver.hpp"
#include "exceptions.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <typeindex>
#include <type_traits>
#include <utility>
#include <vector>

namespace librtdi {

enum class registration_policy {
    multiple,
    single,
    replace,
    skip
};

/// A zero-size tag type that carries a compile-time dependency type list.
/// Used at the registration site to declare service dependencies without macros.
///
/// Usage:
///   registry.add_singleton<IFoo, Foo>(di::deps<IBar, IBaz>);
///
template <typename... Deps>
struct deps_tag {
    using type_list = std::tuple<Deps...>;
    static constexpr std::size_t count = sizeof...(Deps);
};

template <typename... Deps>
inline constexpr deps_tag<Deps...> deps{};

struct build_options {
    bool validate_on_build = true;
    bool validate_scopes   = true;
};

class registry {
public:
    registry();
    ~registry();

    registry(const registry&) = delete;
    registry& operator=(const registry&) = delete;
    registry(registry&&) noexcept;
    registry& operator=(registry&&) noexcept;

    // ---------------------------------------------------------------
    // deps<> auto-wiring — primary form
    // ---------------------------------------------------------------

    template <typename TInterface, typename TImpl, typename... Deps>
    registry& add_singleton(deps_tag<Deps...> /*tag*/,
                            registration_policy policy = registration_policy::multiple) {
        static_assert(std::is_base_of_v<TInterface, TImpl>,
                      "TImpl must derive from TInterface");
        static_assert(std::is_constructible_v<TImpl, std::shared_ptr<Deps>...>,
                      "TImpl is not constructible from shared_ptr<Deps>...");
        return register_component(
            typeid(TInterface), lifetime_kind::singleton,
            [](resolver& r) -> std::shared_ptr<void> {
                return std::make_shared<TImpl>(
                    r.template resolve<Deps>()...);
            },
            {std::type_index(typeid(Deps))...}, {}, policy,
            std::type_index(typeid(TImpl)));
    }

    template <typename TInterface, typename TImpl, typename... Deps>
    registry& add_scoped(deps_tag<Deps...> /*tag*/,
                         registration_policy policy = registration_policy::multiple) {
        static_assert(std::is_base_of_v<TInterface, TImpl>,
                      "TImpl must derive from TInterface");
        static_assert(std::is_constructible_v<TImpl, std::shared_ptr<Deps>...>,
                      "TImpl is not constructible from shared_ptr<Deps>...");
        return register_component(
            typeid(TInterface), lifetime_kind::scoped,
            [](resolver& r) -> std::shared_ptr<void> {
                return std::make_shared<TImpl>(
                    r.template resolve<Deps>()...);
            },
            {std::type_index(typeid(Deps))...}, {}, policy,
            std::type_index(typeid(TImpl)));
    }

    template <typename TInterface, typename TImpl, typename... Deps>
    registry& add_transient(deps_tag<Deps...> /*tag*/,
                            registration_policy policy = registration_policy::multiple) {
        static_assert(std::is_base_of_v<TInterface, TImpl>,
                      "TImpl must derive from TInterface");
        static_assert(std::is_constructible_v<TImpl, std::shared_ptr<Deps>...>,
                      "TImpl is not constructible from shared_ptr<Deps>...");
        return register_component(
            typeid(TInterface), lifetime_kind::transient,
            [](resolver& r) -> std::shared_ptr<void> {
                return std::make_shared<TImpl>(
                    r.template resolve<Deps>()...);
            },
            {std::type_index(typeid(Deps))...}, {}, policy,
            std::type_index(typeid(TImpl)));
    }

    // ---------------------------------------------------------------
    // Zero-dependency shorthand
    // ---------------------------------------------------------------

    template <typename TInterface, typename TImpl>
    registry& add_singleton(registration_policy policy = registration_policy::multiple) {
        static_assert(std::is_base_of_v<TInterface, TImpl>,
                      "TImpl must derive from TInterface");
        static_assert(std::is_constructible_v<TImpl>,
                      "TImpl must be default-constructible for zero-dep registration. "
                      "Use add_singleton<I,T>(di::deps<...>) for components with dependencies.");
        return register_component(
            typeid(TInterface), lifetime_kind::singleton,
            [](resolver&) -> std::shared_ptr<void> {
                return std::make_shared<TImpl>();
            },
            {}, {}, policy,
            std::type_index(typeid(TImpl)));
    }

    template <typename TInterface, typename TImpl>
    registry& add_scoped(registration_policy policy = registration_policy::multiple) {
        static_assert(std::is_base_of_v<TInterface, TImpl>,
                      "TImpl must derive from TInterface");
        static_assert(std::is_constructible_v<TImpl>,
                      "TImpl must be default-constructible for zero-dep registration.");
        return register_component(
            typeid(TInterface), lifetime_kind::scoped,
            [](resolver&) -> std::shared_ptr<void> {
                return std::make_shared<TImpl>();
            },
            {}, {}, policy,
            std::type_index(typeid(TImpl)));
    }

    template <typename TInterface, typename TImpl>
    registry& add_transient(registration_policy policy = registration_policy::multiple) {
        static_assert(std::is_base_of_v<TInterface, TImpl>,
                      "TImpl must derive from TInterface");
        static_assert(std::is_constructible_v<TImpl>,
                      "TImpl must be default-constructible for zero-dep registration.");
        return register_component(
            typeid(TInterface), lifetime_kind::transient,
            [](resolver&) -> std::shared_ptr<void> {
                return std::make_shared<TImpl>();
            },
            {}, {}, policy,
            std::type_index(typeid(TImpl)));
    }

    // ---------------------------------------------------------------
    // Keyed deps<> auto-wiring
    // ---------------------------------------------------------------

    template <typename TInterface, typename TImpl, typename... Deps>
    registry& add_singleton(std::string_view key, deps_tag<Deps...> /*tag*/,
                            registration_policy policy = registration_policy::multiple) {
        static_assert(std::is_base_of_v<TInterface, TImpl>,
                      "TImpl must derive from TInterface");
        static_assert(std::is_constructible_v<TImpl, std::shared_ptr<Deps>...>,
                      "TImpl is not constructible from shared_ptr<Deps>...");
        return register_component(
            typeid(TInterface), lifetime_kind::singleton,
            [](resolver& r) -> std::shared_ptr<void> {
                return std::make_shared<TImpl>(
                    r.template resolve<Deps>()...);
            },
            {std::type_index(typeid(Deps))...},
            std::string(key), policy,
            std::type_index(typeid(TImpl)));
    }

    template <typename TInterface, typename TImpl, typename... Deps>
    registry& add_scoped(std::string_view key, deps_tag<Deps...> /*tag*/,
                         registration_policy policy = registration_policy::multiple) {
        static_assert(std::is_base_of_v<TInterface, TImpl>,
                      "TImpl must derive from TInterface");
        static_assert(std::is_constructible_v<TImpl, std::shared_ptr<Deps>...>,
                      "TImpl is not constructible from shared_ptr<Deps>...");
        return register_component(
            typeid(TInterface), lifetime_kind::scoped,
            [](resolver& r) -> std::shared_ptr<void> {
                return std::make_shared<TImpl>(
                    r.template resolve<Deps>()...);
            },
            {std::type_index(typeid(Deps))...},
            std::string(key), policy,
            std::type_index(typeid(TImpl)));
    }

    template <typename TInterface, typename TImpl, typename... Deps>
    registry& add_transient(std::string_view key, deps_tag<Deps...> /*tag*/,
                            registration_policy policy = registration_policy::multiple) {
        static_assert(std::is_base_of_v<TInterface, TImpl>,
                      "TImpl must derive from TInterface");
        static_assert(std::is_constructible_v<TImpl, std::shared_ptr<Deps>...>,
                      "TImpl is not constructible from shared_ptr<Deps>...");
        return register_component(
            typeid(TInterface), lifetime_kind::transient,
            [](resolver& r) -> std::shared_ptr<void> {
                return std::make_shared<TImpl>(
                    r.template resolve<Deps>()...);
            },
            {std::type_index(typeid(Deps))...},
            std::string(key), policy,
            std::type_index(typeid(TImpl)));
    }

    // ---------------------------------------------------------------
    // Keyed zero-dependency shorthand
    // ---------------------------------------------------------------

    template <typename TInterface, typename TImpl>
    registry& add_singleton(std::string_view key,
                            registration_policy policy = registration_policy::multiple) {
        static_assert(std::is_base_of_v<TInterface, TImpl>,
                      "TImpl must derive from TInterface");
        static_assert(std::is_constructible_v<TImpl>,
                      "TImpl must be default-constructible for zero-dep registration.");
        return register_component(
            typeid(TInterface), lifetime_kind::singleton,
            [](resolver&) -> std::shared_ptr<void> {
                return std::make_shared<TImpl>();
            },
            {},
            std::string(key), policy,
            std::type_index(typeid(TImpl)));
    }

    template <typename TInterface, typename TImpl>
    registry& add_scoped(std::string_view key,
                         registration_policy policy = registration_policy::multiple) {
        static_assert(std::is_base_of_v<TInterface, TImpl>,
                      "TImpl must derive from TInterface");
        static_assert(std::is_constructible_v<TImpl>,
                      "TImpl must be default-constructible for zero-dep registration.");
        return register_component(
            typeid(TInterface), lifetime_kind::scoped,
            [](resolver&) -> std::shared_ptr<void> {
                return std::make_shared<TImpl>();
            },
            {},
            std::string(key), policy,
            std::type_index(typeid(TImpl)));
    }

    template <typename TInterface, typename TImpl>
    registry& add_transient(std::string_view key,
                            registration_policy policy = registration_policy::multiple) {
        static_assert(std::is_base_of_v<TInterface, TImpl>,
                      "TImpl must derive from TInterface");
        static_assert(std::is_constructible_v<TImpl>,
                      "TImpl must be default-constructible for zero-dep registration.");
        return register_component(
            typeid(TInterface), lifetime_kind::transient,
            [](resolver&) -> std::shared_ptr<void> {
                return std::make_shared<TImpl>();
            },
            {},
            std::string(key), policy,
            std::type_index(typeid(TImpl)));
    }

    // ---------------------------------------------------------------
    // forward registration
    // ---------------------------------------------------------------

    template <typename TInterface, typename TTarget>
    registry& forward(registration_policy policy = registration_policy::multiple) {
        static_assert(std::is_base_of_v<TInterface, TTarget>,
                      "TTarget must derive from TInterface");
        return register_component(
            typeid(TInterface), lifetime_kind::transient,
            [](resolver&) -> std::shared_ptr<void> { return nullptr; },
            {std::type_index(typeid(TTarget))}, {}, policy,
            std::type_index(typeid(TTarget)),
            std::type_index(typeid(TTarget)), // forward_target
            // Two-step cast: void* → TTarget* → TInterface*
            [](std::shared_ptr<void> raw) -> std::shared_ptr<void> {
                return std::static_pointer_cast<TInterface>(
                    std::static_pointer_cast<TTarget>(std::move(raw)));
            });
    }

    // ---------------------------------------------------------------
    // Decorator registration
    // ---------------------------------------------------------------

    /// decorate all implementations of I with D (no extra deps).
    template <typename TInterface, typename TDecorator>
    registry& decorate() {
        static_assert(std::is_base_of_v<TInterface, TDecorator>,
                      "TDecorator must derive from TInterface");
        static_assert(std::is_constructible_v<TDecorator, std::shared_ptr<TInterface>>,
                      "TDecorator must be constructible from shared_ptr<TInterface>");
        return register_decorator(
            typeid(TInterface), std::nullopt,
            [](factory_fn inner) -> factory_fn {
                return [inner = std::move(inner)](resolver& r) -> std::shared_ptr<void> {
                    auto raw = inner(r);
                    return std::make_shared<TDecorator>(
                        std::static_pointer_cast<TInterface>(raw));
                };
            },
            {});
    }

    /// decorate all implementations of I with D (with extra deps).
    template <typename TInterface, typename TDecorator, typename... Extra>
    registry& decorate(deps_tag<Extra...> /*tag*/) {
        static_assert(std::is_base_of_v<TInterface, TDecorator>,
                      "TDecorator must derive from TInterface");
        static_assert(std::is_constructible_v<TDecorator, std::shared_ptr<TInterface>, std::shared_ptr<Extra>...>,
                      "TDecorator must be constructible from shared_ptr<TInterface>, shared_ptr<Extra>...");
        return register_decorator(
            typeid(TInterface), std::nullopt,
            [](factory_fn inner) -> factory_fn {
                return [inner = std::move(inner)](resolver& r) -> std::shared_ptr<void> {
                    auto raw = inner(r);
                    return std::make_shared<TDecorator>(
                        std::static_pointer_cast<TInterface>(raw),
                        r.template resolve<Extra>()...);
                };
            },
            {std::type_index(typeid(Extra))...});
    }

    /// decorate a specific implementation (identified by type_index) of I with D.
    template <typename TInterface, typename TDecorator>
    registry& decorate(std::type_index target) {
        static_assert(std::is_base_of_v<TInterface, TDecorator>,
                      "TDecorator must derive from TInterface");
        static_assert(std::is_constructible_v<TDecorator, std::shared_ptr<TInterface>>,
                      "TDecorator must be constructible from shared_ptr<TInterface>");
        return register_decorator(
            typeid(TInterface), target,
            [](factory_fn inner) -> factory_fn {
                return [inner = std::move(inner)](resolver& r) -> std::shared_ptr<void> {
                    auto raw = inner(r);
                    return std::make_shared<TDecorator>(
                        std::static_pointer_cast<TInterface>(raw));
                };
            },
            {});
    }

    /// decorate a specific implementation of I with D (with extra deps).
    template <typename TInterface, typename TDecorator, typename... Extra>
    registry& decorate(std::type_index target, deps_tag<Extra...> /*tag*/) {
        static_assert(std::is_base_of_v<TInterface, TDecorator>,
                      "TDecorator must derive from TInterface");
        static_assert(std::is_constructible_v<TDecorator, std::shared_ptr<TInterface>, std::shared_ptr<Extra>...>,
                      "TDecorator must be constructible from shared_ptr<TInterface>, shared_ptr<Extra>...");
        return register_decorator(
            typeid(TInterface), target,
            [](factory_fn inner) -> factory_fn {
                return [inner = std::move(inner)](resolver& r) -> std::shared_ptr<void> {
                    auto raw = inner(r);
                    return std::make_shared<TDecorator>(
                        std::static_pointer_cast<TInterface>(raw),
                        r.template resolve<Extra>()...);
                };
            },
            {std::type_index(typeid(Extra))...});
    }

    // ---------------------------------------------------------------
    // build
    // ---------------------------------------------------------------

    std::shared_ptr<resolver> build(build_options options = {});

    // Access currently registered descriptors (for testing/diagnostics).
    // Primarily useful before build(); build() moves descriptors into resolver.
    const std::vector<descriptor>& descriptors() const;

private:
    registry& register_component(std::type_index type, lifetime_kind lifetime,
                                 factory_fn factory,
                       std::vector<std::type_index> deps,
                       std::string key = {},
                       registration_policy policy = registration_policy::multiple,
                       std::optional<std::type_index> impl_type = std::nullopt,
                       std::optional<std::type_index> forward_target = std::nullopt,
                       descriptor::forward_cast_fn forward_cast = nullptr);

    using decorator_wrapper = std::function<factory_fn(factory_fn)>;

    registry& register_decorator(std::type_index interface_type,
                                 std::optional<std::type_index> target_impl,
                                 decorator_wrapper wrapper,
                                 std::vector<std::type_index> extra_deps);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace librtdi
