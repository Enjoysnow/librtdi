#include "librtdi/resolver.hpp"
#include "librtdi/exceptions.hpp"
#include "stacktrace_utils.hpp"

#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>
#include <typeindex>
#include <string>
#include <functional>
#include <utility>
#include <tuple>

namespace librtdi {

// ---------------------------------------------------------------
// Slot key: (type, key, lifetime, is_collection) → identifies a slot
// ---------------------------------------------------------------

using slot_key = std::tuple<std::type_index, std::string, lifetime_kind, bool>;

// ---------------------------------------------------------------
// Impl — shared resolver state
// ---------------------------------------------------------------

struct resolver::impl {
    std::vector<descriptor> descriptors;

    // Index: slot_key → list of descriptor indices in that slot
    std::map<slot_key, std::vector<std::size_t>> slot_to_indices;

    // Singleton cache: descriptor index → erased_ptr
    std::recursive_mutex singleton_mutex;
    std::unordered_map<std::size_t, erased_ptr> singletons;
    std::vector<std::size_t> creation_order;

    explicit impl(std::vector<descriptor> descs)
        : descriptors(std::move(descs))
    {
        for (std::size_t i = 0; i < descriptors.size(); ++i) {
            auto& d = descriptors[i];
            auto sk = slot_key(d.component_type, d.key, d.lifetime, d.is_collection);
            slot_to_indices[sk].push_back(i);
        }
    }

    ~impl() noexcept {
        teardown_singletons();
    }

    const std::vector<std::size_t>* find_slot(std::type_index type,
                                              const std::string& key,
                                              lifetime_kind lt,
                                              bool is_coll) const {
        auto it = slot_to_indices.find(slot_key(type, key, lt, is_coll));
        if (it == slot_to_indices.end() || it->second.empty()) return nullptr;
        return &it->second;
    }

    std::vector<std::size_t> singleton_dependencies_for(std::size_t idx,
                                                         bool& inconsistent) const noexcept {
        std::vector<std::size_t> deps;
        if (idx >= descriptors.size()) {
            inconsistent = true;
            return deps;
        }

        const auto& desc = descriptors[idx];
        for (const auto& dep : desc.dependencies) {
            if (dep.is_transient) {
                continue;
            }

            const auto* dep_indices = find_slot(dep.type, std::string{},
                                                lifetime_kind::singleton,
                                                dep.is_collection);
            if (!dep_indices) {
                continue;
            }

            if (!dep.is_collection && dep_indices->size() != 1U) {
                inconsistent = true;
            }

            for (auto dep_idx : *dep_indices) {
                if (dep_idx == idx) {
                    inconsistent = true;
                    continue;
                }

                auto it = singletons.find(dep_idx);
                if (it == singletons.end()) {
                    continue;
                }

                bool seen = false;
                for (auto existing : deps) {
                    if (existing == dep_idx) {
                        seen = true;
                        break;
                    }
                }
                if (!seen) {
                    deps.push_back(dep_idx);
                }
            }
        }

        return deps;
    }

    std::vector<std::size_t> dependency_aware_teardown_order() const noexcept {
        if (creation_order.empty()) {
            return {};
        }

        std::vector<unsigned char> visit_state(descriptors.size(), 0);
        std::vector<std::size_t> order;
        order.reserve(creation_order.size());

        bool inconsistent = false;
        std::function<void(std::size_t)> visit = [&](std::size_t idx) {
            if (idx >= visit_state.size()) {
                inconsistent = true;
                return;
            }

            if (singletons.find(idx) == singletons.end()) {
                return;
            }

            auto& state = visit_state[idx];
            if (state == 2) {
                return;
            }
            if (state == 1) {
                inconsistent = true;
                return;
            }

            state = 1;
            order.push_back(idx);

            for (auto dep_idx : singleton_dependencies_for(idx, inconsistent)) {
                visit(dep_idx);
            }

            state = 2;
        };

        for (auto it = creation_order.rbegin(); it != creation_order.rend(); ++it) {
            visit(*it);
        }

        if (inconsistent) {
            return {};
        }

        return order;
    }

    void reset_singleton_entry(std::size_t idx,
                               std::vector<unsigned char>& reset_state) noexcept {
        if (idx >= reset_state.size() || reset_state[idx] != 0) {
            return;
        }

        auto it = singletons.find(idx);
        if (it == singletons.end()) {
            return;
        }

        it->second.reset();
        reset_state[idx] = 1;
    }

    void teardown_singletons() noexcept {
        std::lock_guard lock(singleton_mutex);
        if (singletons.empty()) {
            creation_order.clear();
            return;
        }

        std::vector<unsigned char> reset_state(descriptors.size(), 0);

        for (auto idx : dependency_aware_teardown_order()) {
            reset_singleton_entry(idx, reset_state);
        }

        for (auto it = creation_order.rbegin(); it != creation_order.rend(); ++it) {
            reset_singleton_entry(*it, reset_state);
        }

        singletons.clear();
        creation_order.clear();
    }
};

// ---------------------------------------------------------------
// Constructors / Destructor
// ---------------------------------------------------------------

resolver::resolver(std::unique_ptr<impl> p_impl)
    : impl_(std::move(p_impl))
{}

resolver::~resolver() = default;

std::shared_ptr<resolver> resolver::create(std::vector<descriptor> descriptors) {
    auto uni = std::make_unique<impl>(std::move(descriptors));
    return std::shared_ptr<resolver>(new resolver(std::move(uni)));
}

// ---------------------------------------------------------------
// Internal: resolve a singleton descriptor by index
// ---------------------------------------------------------------

void* resolver::resolve_singleton_by_index(std::size_t idx) {
    if (idx >= impl_->descriptors.size()) {
        throw di_error("descriptor index out of range");
    }
    const auto& desc = impl_->descriptors[idx];

    std::lock_guard lock(impl_->singleton_mutex);
    auto it = impl_->singletons.find(idx);
    if (it != impl_->singletons.end()) {
        return it->second.get();
    }

    erased_ptr instance;
    try {
        instance = desc.factory(*this);
    } catch (di_error& e) {
        // Annotate with resolution context so nested failures show the
        // full chain: "... (while resolving B -> A)". We intentionally catch
        // di_error by non-const reference so we can enrich the exception
        // (e.g., append_resolution_context / set_diagnostic_detail) before
        // rethrowing it.
        std::string ctx = internal::demangle(desc.component_type);
        if (desc.impl_type.has_value()) {
            ctx += " [impl: " + internal::demangle(desc.impl_type.value()) + "]";
        }
        e.append_resolution_context(ctx);
        if (e.diagnostic_detail().empty()) {
            auto trace = internal::format_registration_trace(desc);
            if (!trace.empty()) e.set_diagnostic_detail(trace);
        }
        throw;
    } catch (const std::exception& e) {
        auto ex = resolution_error(desc.component_type, e,
                               desc.registration_location,
                               std::source_location::current());
        ex.set_diagnostic_detail(
            internal::format_registration_trace(desc));
        throw ex;
    }

    auto [created_it, inserted] = impl_->singletons.emplace(idx, std::move(instance));
    if (inserted) {
        impl_->creation_order.push_back(idx);
    }
    return created_it->second.get();
}

erased_ptr resolver::resolve_transient_by_index(std::size_t idx) {
    if (idx >= impl_->descriptors.size()) {
        throw di_error("descriptor index out of range");
    }
    const auto& desc = impl_->descriptors[idx];

    try {
        return desc.factory(*this);
    } catch (di_error& e) {
        // Annotate with resolution context so nested failures show the
        // full chain: "... (while resolving B -> A)"
        std::string ctx = internal::demangle(desc.component_type);
        if (desc.impl_type.has_value()) {
            ctx += " [impl: " + internal::demangle(desc.impl_type.value()) + "]";
        }
        e.append_resolution_context(ctx);
        if (e.diagnostic_detail().empty()) {
            auto trace = internal::format_registration_trace(desc);
            if (!trace.empty()) e.set_diagnostic_detail(trace);
        }
        throw;
    } catch (const std::exception& e) {
        auto ex = resolution_error(desc.component_type, e,
                               desc.registration_location,
                               std::source_location::current());
        ex.set_diagnostic_detail(
            internal::format_registration_trace(desc));
        throw ex;
    }
}

// ---------------------------------------------------------------
// Non-template core: get singleton
// ---------------------------------------------------------------

void* resolver::get_singleton_impl(std::type_index type, const std::string& key) {
    const auto* indices = impl_->find_slot(type, key, lifetime_kind::singleton, false);
    if (!indices || indices->empty()) return nullptr;
    // Single-instance slot — should have exactly 1 entry
    return resolve_singleton_by_index(indices->front());
}

// ---------------------------------------------------------------
// Non-template core: create transient
// ---------------------------------------------------------------

erased_ptr resolver::create_transient_impl(std::type_index type, const std::string& key) {
    const auto* indices = impl_->find_slot(type, key, lifetime_kind::transient, false);
    if (!indices || indices->empty()) return {};
    return resolve_transient_by_index(indices->front());
}

// ---------------------------------------------------------------
// Non-template core: get singleton collection
// ---------------------------------------------------------------

std::vector<void*> resolver::get_collection_impl(std::type_index type,
                                                  const std::string& key) {
    const auto* indices = impl_->find_slot(type, key, lifetime_kind::singleton, true);
    if (!indices) return {};

    std::vector<void*> result;
    result.reserve(indices->size());
    for (auto idx : *indices) {
        result.push_back(resolve_singleton_by_index(idx));
    }
    return result;
}

// ---------------------------------------------------------------
// Non-template core: create transient collection
// ---------------------------------------------------------------

std::vector<erased_ptr> resolver::create_collection_impl(std::type_index type,
                                                          const std::string& key) {
    const auto* indices = impl_->find_slot(type, key, lifetime_kind::transient, true);
    if (!indices) return {};

    std::vector<erased_ptr> result;
    result.reserve(indices->size());
    for (auto idx : *indices) {
        result.push_back(resolve_transient_by_index(idx));
    }
    return result;
}

// ---------------------------------------------------------------
// Diagnostic: slot hint for better not_found messages
// ---------------------------------------------------------------

std::string resolver::slot_hint(std::type_index type, const std::string& key,
                                const char* attempted_method) const {
    struct slot_info {
        lifetime_kind lt;
        bool is_coll;
        const char* description;
        const char* suggestion;
    };

    static constexpr slot_info slots[] = {
        { lifetime_kind::singleton, false, "singleton",            "get<T>()" },
        { lifetime_kind::transient, false, "transient",            "create<T>()" },
        { lifetime_kind::singleton, true,  "singleton collection", "get_all<T>()" },
        { lifetime_kind::transient, true,  "transient collection", "create_all<T>()" },
    };

    std::string hints;
    for (const auto& s : slots) {
        if (impl_->find_slot(type, key, s.lt, s.is_coll)) {
            if (!hints.empty()) hints += ", ";
            hints += std::string(s.description) + " (use " + s.suggestion + ")";
        }
    }

    if (hints.empty()) return {};
    return std::string("type is registered as ") + hints
           + " but was requested via " + attempted_method;
}

} // namespace librtdi
