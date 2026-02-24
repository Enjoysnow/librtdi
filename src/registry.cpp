#include "librtdi/registry.hpp"
#include "librtdi/resolver.hpp"

#include <algorithm>
#include <cassert>
#include <optional>
#include <set>
#include <vector>
#include <typeindex>
#include <stdexcept>

namespace librtdi {

void validate_descriptors(const std::vector<descriptor>& descriptors,
                          const build_options& options);

// ---------------------------------------------------------------
// Impl
// ---------------------------------------------------------------

struct registry::Impl {
    std::vector<descriptor> descriptors;
    bool built = false;

    // Decorator entries stored until build() applies them
    struct DecoratorEntry {
        std::type_index interface_type;
        std::optional<std::type_index> target_impl;
        registry::decorator_wrapper wrapper;
        std::vector<dependency_info> extra_deps;
    };
    std::vector<DecoratorEntry> decorators;

    // Forward entries stored until build() expands them
    struct ForwardEntry {
        std::type_index interface_type;
        std::type_index target_type;
        descriptor::forward_cast_fn cast;
        void (*forward_deleter)(void*) = nullptr;
    };
    std::vector<ForwardEntry> forwards;

    // Check if a single-instance slot is already occupied
    bool has_single(std::type_index type, const std::string& key,
                    lifetime_kind lt) const {
        return std::any_of(descriptors.begin(), descriptors.end(),
            [&](const descriptor& d) {
                return d.component_type == type && d.key == key
                    && d.lifetime == lt && !d.is_collection;
            });
    }
};

// ---------------------------------------------------------------
// Constructors / Destructor / Move
// ---------------------------------------------------------------

registry::registry()
    : impl_(std::make_unique<Impl>())
{}

registry::~registry() = default;

registry::registry(registry&&) noexcept = default;
registry& registry::operator=(registry&&) noexcept = default;

// ---------------------------------------------------------------
// Single-instance slot registration
// ---------------------------------------------------------------

registry& registry::register_single(
        std::type_index type, lifetime_kind lifetime,
        factory_fn factory, std::vector<dependency_info> deps,
        std::string key, std::optional<std::type_index> impl_type) {
    if (impl_->built) {
        throw di_error("Cannot register components after build() has been called");
    }
    if (!factory) {
        throw di_error("Component factory cannot be empty");
    }

    // Check uniqueness for single-instance slots
    if (impl_->has_single(type, key, lifetime)) {
        if (key.empty()) {
            throw duplicate_registration(type);
        } else {
            throw duplicate_registration(type, key);
        }
    }

    impl_->descriptors.push_back(descriptor{
        type, lifetime, std::move(factory), std::move(deps),
        std::move(key), /*is_collection=*/false, std::move(impl_type),
        std::nullopt, nullptr
    });
    return *this;
}

// ---------------------------------------------------------------
// Collection slot registration
// ---------------------------------------------------------------

registry& registry::register_collection(
        std::type_index type, lifetime_kind lifetime,
        factory_fn factory, std::vector<dependency_info> deps,
        std::string key, std::optional<std::type_index> impl_type) {
    if (impl_->built) {
        throw di_error("Cannot register components after build() has been called");
    }
    if (!factory) {
        throw di_error("Component factory cannot be empty");
    }

    impl_->descriptors.push_back(descriptor{
        type, lifetime, std::move(factory), std::move(deps),
        std::move(key), /*is_collection=*/true, std::move(impl_type),
        std::nullopt, nullptr
    });
    return *this;
}

// ---------------------------------------------------------------
// Forward registration (deferred to build)
// ---------------------------------------------------------------

registry& registry::register_forward(
        std::type_index interface_type,
        std::type_index target_type,
        descriptor::forward_cast_fn cast,
        void (*forward_deleter)(void*)) {
    if (impl_->built) {
        throw di_error("Cannot register components after build() has been called");
    }
    impl_->forwards.push_back({interface_type, target_type, std::move(cast),
                               forward_deleter});
    return *this;
}

// ---------------------------------------------------------------
// Decorator registration (deferred to build)
// ---------------------------------------------------------------

registry& registry::register_decorator(
        std::type_index interface_type,
        std::optional<std::type_index> target_impl,
        decorator_wrapper wrapper,
        std::vector<dependency_info> extra_deps) {
    if (impl_->built) {
        throw di_error("Cannot register decorators after build() has been called");
    }
    impl_->decorators.push_back({interface_type, std::move(target_impl),
                                  std::move(wrapper), std::move(extra_deps)});
    return *this;
}

const std::vector<descriptor>& registry::descriptors() const {
    return impl_->descriptors;
}

// ---------------------------------------------------------------
// build
// ---------------------------------------------------------------

std::shared_ptr<resolver> registry::build(build_options options) {
    if (impl_->built) {
        throw di_error("build() can only be called once");
    }

    // ① Forward expansion: for each forward entry, replicate all matching
    //    target descriptors (all 4 slots) under the interface type.
    {
        std::vector<descriptor> expanded;
        for (auto& fwd : impl_->forwards) {
            bool found_any = false;
            for (std::size_t i = 0; i < impl_->descriptors.size(); ++i) {
                auto& target = impl_->descriptors[i];
                if (target.component_type != fwd.target_type) continue;
                if (!target.key.empty()) continue; // forward only expands non-keyed

                found_any = true;
                std::size_t target_idx = i;
                auto cast = fwd.cast;

                if (target.lifetime == lifetime_kind::singleton) {
                    // Singleton: delegate to the same cached instance
                    expanded.push_back(descriptor{
                        fwd.interface_type,
                        lifetime_kind::singleton,
                        [target_idx, cast](resolver& r) -> erased_ptr {
                            // Resolve the target singleton, then cast pointer
                            void* raw = r.resolve_singleton_by_index(target_idx);
                            void* casted = cast(raw);
                            // Return a non-owning erased_ptr (no deleter)
                            // because the target owns the instance
                            return erased_ptr(casted, nullptr);
                        },
                        { dependency_info{fwd.target_type, target.is_collection, false} },
                        std::string{},
                        target.is_collection,
                        target.impl_type,
                        fwd.target_type,
                        cast
                    });
                } else {
                    // Transient: create fresh via target, then cast.
                    // The forward_deleter performs `delete static_cast<TInterface*>(p)`
                    // which works correctly under all inheritance models (single, MI,
                    // virtual) because TInterface has a virtual destructor.
                    auto fwd_deleter = fwd.forward_deleter;
                    expanded.push_back(descriptor{
                        fwd.interface_type,
                        lifetime_kind::transient,
                        [target_idx, cast, fwd_deleter](resolver& r) -> erased_ptr {
                            auto ep = r.resolve_transient_by_index(target_idx);
                            void* original = ep.release();
                            void* casted = cast(original);
                            return erased_ptr(casted, fwd_deleter);
                        },
                        { dependency_info{fwd.target_type, target.is_collection,
                                          target.lifetime == lifetime_kind::transient} },
                        std::string{},
                        target.is_collection,
                        target.impl_type,
                        fwd.target_type,
                        cast
                    });
                }
            }
            if (!found_any) {
                // Add placeholder so validation detects missing dependency
                expanded.push_back(descriptor{
                    fwd.interface_type, lifetime_kind::transient,
                    [](resolver&) -> erased_ptr { return {}; },
                    { dependency_info{fwd.target_type, false, false} },
                    std::string{},
                    false, std::nullopt,
                    fwd.target_type, nullptr
                });
            }
        }

        for (auto& desc : expanded) {
            impl_->descriptors.push_back(std::move(desc));
        }
    }

    // ② Apply decorators: wrap descriptor factories in registered order.
    //    Skip forward-expanded singleton descriptors: their factories return
    //    non-owning erased_ptr (deleter==nullptr), but decorator wrappers
    //    assume ownership via unique_ptr — applying a decorator would cause
    //    double-free on resolver destruction.
    for (auto& dec : impl_->decorators) {
        for (auto& desc : impl_->descriptors) {
            if (desc.component_type != dec.interface_type) continue;

            // Forward-expanded singletons must not be decorated (ownership conflict)
            if (desc.forward_target.has_value() &&
                desc.lifetime == lifetime_kind::singleton) {
                continue;
            }

            // Check if this decorator targets a specific impl
            if (dec.target_impl.has_value()) {
                if (!desc.impl_type.has_value() ||
                    desc.impl_type.value() != dec.target_impl.value()) {
                    continue;
                }
            }

            // Wrap the factory
            desc.factory = dec.wrapper(std::move(desc.factory));

            // Append extra dependencies
            for (auto& dep : dec.extra_deps) {
                desc.dependencies.push_back(dep);
            }
        }
    }

    // ③ Validate before building
    if (options.validate_on_build) {
        validate_descriptors(impl_->descriptors, options);
    }

    impl_->built = true;

    return resolver::create(std::move(impl_->descriptors));
}

} // namespace librtdi
