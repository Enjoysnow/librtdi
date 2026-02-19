#include "librtdi/registry.hpp"
#include "librtdi/resolver.hpp"
#include "librtdi/scope.hpp"

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

namespace {

enum class policy_action_kind {
    go_ahead,
    skip,
    lock_only_upgrade
};

[[noreturn]] void throw_duplicate(std::type_index type, const std::string& key) {
    if (key.empty()) {
        throw duplicate_registration(type);
    }
    throw duplicate_registration(type, key);
}

std::size_t count_in_slot(const std::vector<descriptor>& descriptors,
                        std::type_index type,
                        const std::string& key) {
    return static_cast<std::size_t>(std::count_if(
        descriptors.begin(), descriptors.end(),
        [&](const descriptor& d) {
            return d.component_type == type && d.key == key;
        }));
}

bool has_any_in_slot(const std::vector<descriptor>& descriptors,
                  std::type_index type,
                  const std::string& key) {
    return std::any_of(descriptors.begin(), descriptors.end(),
        [&](const descriptor& d) {
            return d.component_type == type && d.key == key;
        });
}

void mark_slot_single(std::vector<descriptor>& descriptors,
                    std::type_index type,
                    const std::string& key) {
    assert(count_in_slot(descriptors, type, key) == 1 &&
           "MarkSlotSingle requires exactly one descriptor in slot");
    for (auto& d : descriptors) {
        if (d.component_type == type && d.key == key) {
            d.is_single_slot = true;
            break;
        }
    }
}

policy_action_kind apply_policy(
    std::vector<descriptor>& descriptors,
        std::set<std::pair<std::type_index, std::string>>& single_locked,
        std::type_index type,
        const std::string& key,
    registration_policy policy) {
    auto slot = std::make_pair(type, key);

    switch (policy) {
        case registration_policy::multiple: {
            if (single_locked.contains(slot)) {
                throw_duplicate(type, key);
            }
            return policy_action_kind::go_ahead;
        }

        case registration_policy::single: {
            std::size_t existing = count_in_slot(descriptors, type, key);
            if (single_locked.contains(slot)) {
                throw_duplicate(type, key);
            }

            if (existing == 0) {
                single_locked.insert(slot);
                return policy_action_kind::go_ahead;
            }

            if (existing == 1) {
                mark_slot_single(descriptors, type, key);
                single_locked.insert(slot);
                return policy_action_kind::lock_only_upgrade;
            }

            throw_duplicate(type, key);
        }

        case registration_policy::replace: {
            bool keep_single_lock = single_locked.contains(slot);
            std::erase_if(descriptors, [&](const descriptor& d) {
                return d.component_type == type && d.key == key;
            });
            if (keep_single_lock) {
                single_locked.insert(slot);
            }
            return policy_action_kind::go_ahead;
        }

        case registration_policy::skip: {
            if (has_any_in_slot(descriptors, type, key)) {
                return policy_action_kind::skip;
            }
            return policy_action_kind::go_ahead;
        }
    }

    throw di_error("Invalid registration_policy");
}

} // namespace

// ---------------------------------------------------------------
// Impl
// ---------------------------------------------------------------

struct registry::Impl {
    std::vector<descriptor> descriptors;
    bool built = false;

    // (type, key) pairs locked by registration_policy::single.
    // Once locked, subsequent Multiple/Single registrations for the same
    // (type, key) will throw duplicate_registration.
    // Replace can override a locked slot; Skip silently skips.
    std::set<std::pair<std::type_index, std::string>> single_locked;

    // Decorator entries stored until build() applies them
    struct DecoratorEntry {
        std::type_index interface_type;
        std::optional<std::type_index> target_impl;  // nullopt = all implementations
        registry::decorator_wrapper wrapper;
        std::vector<std::type_index> extra_deps;
    };
    std::vector<DecoratorEntry> decorators;
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
// Non-template registration core
// ---------------------------------------------------------------

registry& registry::register_component(
        std::type_index type, lifetime_kind lifetime,
    factory_fn factory, std::vector<std::type_index> deps,
        std::string key, registration_policy policy,
        std::optional<std::type_index> impl_type,
        std::optional<std::type_index> forward_target,
        descriptor::forward_cast_fn forward_cast) {
    if (impl_->built) {
        throw di_error("Cannot register components after build() has been called");
    }

    if (!forward_target.has_value() && !factory) {
        throw di_error("Component factory cannot be empty");
    }
    if (forward_target.has_value() && !key.empty()) {
        throw di_error("forward currently supports non-keyed registrations only");
    }
    if (forward_target.has_value() && !forward_cast) {
        throw di_error("forward descriptor requires a forward_cast function");
    }

    auto action = apply_policy(impl_->descriptors, impl_->single_locked, type, key, policy);
    if (action == policy_action::skip || action == policy_action::lock_only_upgrade) {
        return *this;
    }

    auto slot = std::make_pair(type, key);
    bool mark_single = impl_->single_locked.contains(slot) || policy == registration_policy::single;
    impl_->descriptors.emplace_back(type, lifetime, std::move(factory),
                                     std::move(deps), std::move(key),
                                     mark_single,
                                     std::move(impl_type), std::move(forward_target),
                                     std::move(forward_cast));
    return *this;
}

registry& registry::register_decorator(
        std::type_index interface_type,
        std::optional<std::type_index> target_impl,
        decorator_wrapper wrapper,
        std::vector<std::type_index> extra_deps) {
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

    // ① forward expansion: replace forward placeholders with concrete
    //    descriptors — one per matching target registration.
    {
        struct ForwardEntry {
            std::type_index interface_type;
            std::type_index target_type;
            descriptor::forward_cast_fn cast;
        };
        std::vector<ForwardEntry> forwards;
        for (auto& desc : impl_->descriptors) {
            if (desc.forward_target.has_value()) {
                forwards.push_back({desc.component_type,
                                    desc.forward_target.value(),
                                    desc.forward_cast});
            }
        }

        // Remove all forward placeholders
        std::erase_if(impl_->descriptors, [](const descriptor& d) {
            return d.forward_target.has_value();
        });

        // Expand each forward into N descriptors (one per non-keyed target)
        std::vector<descriptor> expanded;
        for (auto& fwd : forwards) {
            bool found_any = false;
            for (std::size_t i = 0; i < impl_->descriptors.size(); ++i) {
                auto& target = impl_->descriptors[i];
                if (target.component_type != fwd.target_type) continue;
                if (!target.key.empty()) continue;

                found_any = true;
                std::size_t target_idx = i;
                auto cast = fwd.cast;  // capture the two-step cast function
                expanded.emplace_back(
                    fwd.interface_type,
                    target.lifetime,
                    [target_idx, cast](resolver& r) -> std::shared_ptr<void> {
                        auto raw = r.resolve_by_index(target_idx);
                        return cast ? cast(std::move(raw)) : raw;
                    },
                    std::vector<std::type_index>{fwd.target_type},
                    std::string{},
                    target.is_single_slot,
                    target.impl_type,
                    fwd.target_type  // preserve forward_target for validation
                );
            }
            if (!found_any) {
                // No matching targets — add placeholder so validation
                // can detect the missing dependency.
                expanded.emplace_back(
                    fwd.interface_type, lifetime_kind::transient,
                    [](resolver&) -> std::shared_ptr<void> {
                        return nullptr;
                    },
                    std::vector<std::type_index>{fwd.target_type},
                    std::string{},
                    false,
                    std::nullopt,
                    fwd.target_type
                );
            }
        }

        for (auto& desc : expanded) {
            impl_->descriptors.push_back(std::move(desc));
        }

        // Single-slot integrity check after forward expansion.
        // If a slot is locked by Single, final descriptor count in that slot
        // must remain <= 1.
        for (auto& slot : impl_->single_locked) {
            std::size_t count = static_cast<std::size_t>(std::count_if(
                impl_->descriptors.begin(), impl_->descriptors.end(),
                [&](const descriptor& d) {
                    return d.component_type == slot.first && d.key == slot.second;
                }));
            if (count > 1) {
                if (slot.second.empty()) {
                    throw duplicate_registration(slot.first);
                } else {
                    throw duplicate_registration(slot.first, slot.second);
                }
            }

            // Keep descriptor-level visibility in sync with slot lock state.
            for (auto& d : impl_->descriptors) {
                if (d.component_type == slot.first && d.key == slot.second) {
                    d.is_single_slot = true;
                }
            }
        }
    }

    // ② Apply decorators: wrap descriptor factories in registered order.
    for (auto& dec : impl_->decorators) {
        for (auto& desc : impl_->descriptors) {
            if (desc.component_type != dec.interface_type) continue;

            // Check if this decorator targets a specific impl
            if (dec.target_impl.has_value()) {
                // Only apply if this descriptor's impl_type matches
                if (!desc.impl_type.has_value() ||
                    desc.impl_type.value() != dec.target_impl.value()) {
                    continue;
                }
            }
            // else: global decorator — apply to all implementations of this interface

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
