#include "librtdi/resolver.hpp"
#include "librtdi/scope.hpp"
#include "librtdi/exceptions.hpp"

#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <typeindex>
#include <string>
#include <utility>

namespace librtdi {

// ---------------------------------------------------------------
// Impl — shared between root and scoped resolvers
// ---------------------------------------------------------------

struct resolver::impl {
    // The frozen registry (moved from registry on build)
    std::vector<descriptor> descriptors;

    // Composite index: (type_index, key) → list of descriptor indices
    // Using std::map with std::pair for ordered composite key.
    std::map<std::pair<std::type_index, std::string>, std::vector<std::size_t>> type_key_to_indices;

    // Singleton cache keyed by descriptor index — shared across root and all scoped resolvers.
    // Uses recursive_mutex because a singleton factory may resolve other singletons.
    std::recursive_mutex singleton_mutex;
    std::unordered_map<std::size_t, std::shared_ptr<void>> singletons;

    explicit impl(std::vector<descriptor> descs)
        : descriptors(std::move(descs))
    {
        for (std::size_t i = 0; i < descriptors.size(); ++i) {
            auto composite_key = std::make_pair(
                descriptors[i].component_type, descriptors[i].key);
            type_key_to_indices[composite_key].push_back(i);
        }
    }

    // Find the *last* descriptor index matching the given (type, key) — last-wins.
    // Returns the descriptor pointer and sets out_index; nullptr if not found.
    const descriptor* find_last(std::type_index type, const std::string& key,
                                std::size_t& out_index) const {
        auto it = type_key_to_indices.find(std::make_pair(type, key));
        if (it == type_key_to_indices.end() || it->second.empty()) return nullptr;
        out_index = it->second.back();
        return &descriptors[out_index];
    }

    // Return all descriptor indices matching the given (type, key)
    const std::vector<std::size_t>* find_all_indices(std::type_index type,
                                                     const std::string& key) const {
        auto it = type_key_to_indices.find(std::make_pair(type, key));
        if (it == type_key_to_indices.end()) return nullptr;
        return &it->second;
    }
};

// ---------------------------------------------------------------
// ScopedCache — per-scope instance cache, keyed by descriptor index
// ---------------------------------------------------------------

struct resolver::scoped_cache {
    std::recursive_mutex mutex;
    std::unordered_map<std::size_t, std::shared_ptr<void>> instances;
};

// ---------------------------------------------------------------
// Constructors / Destructor
// ---------------------------------------------------------------

resolver::resolver(std::unique_ptr<impl> p_impl)
    : impl_(std::move(p_impl))
    , is_scoped_(false)
    , scoped_cache_(nullptr)
{}

resolver::resolver(std::shared_ptr<impl> shared_impl, bool /*is_scoped*/)
    : impl_(std::move(shared_impl))
    , is_scoped_(true)
    , scoped_cache_(std::make_unique<scoped_cache>())
{}

resolver::~resolver() = default;

// Static factory
std::shared_ptr<resolver> resolver::create(
    std::vector<descriptor> descriptors) {
    auto uni = std::make_unique<impl>(std::move(descriptors));
    return std::shared_ptr<resolver>(new resolver(std::move(uni)));
}

bool resolver::is_root() const noexcept {
    return !is_scoped_;
}

// ---------------------------------------------------------------
// scope creation
// ---------------------------------------------------------------

std::unique_ptr<scope> resolver::create_scope() {
    auto scoped_resolver = std::shared_ptr<resolver>(
        new resolver(impl_, true));
    return std::unique_ptr<scope>(
        new scope(std::move(scoped_resolver)));
}

// ---------------------------------------------------------------
// Internal: resolve a single descriptor by index
// ---------------------------------------------------------------

std::shared_ptr<void> resolver::resolve_by_index(std::size_t idx) {
    if (idx >= impl_->descriptors.size()) {
        throw di_error("descriptor index out of range");
    }

    const auto& desc = impl_->descriptors[idx];

    switch (desc.lifetime) {
        case lifetime_kind::singleton: {
            // Hold recursive_mutex for the entire creation to guarantee
            // exactly-once construction under concurrency.
            std::lock_guard lock(impl_->singleton_mutex);
            auto it = impl_->singletons.find(idx);
            if (it != impl_->singletons.end()) {
                return it->second;
            }
            std::shared_ptr<void> instance;
            try {
                instance = desc.factory(*this);
            } catch (const di_error&) {
                throw;
            } catch (const std::exception& e) {
                throw resolution_error(desc.component_type, e);
            }
            impl_->singletons[idx] = instance;
            return instance;
        }

        case lifetime_kind::scoped: {
            if (!is_scoped_ || !scoped_cache_) {
                throw no_active_scope(desc.component_type);
            }
            std::lock_guard lock(scoped_cache_->mutex);
            auto it = scoped_cache_->instances.find(idx);
            if (it != scoped_cache_->instances.end()) {
                return it->second;
            }
            std::shared_ptr<void> instance;
            try {
                instance = desc.factory(*this);
            } catch (const di_error&) {
                throw;
            } catch (const std::exception& e) {
                throw resolution_error(desc.component_type, e);
            }
            scoped_cache_->instances[idx] = instance;
            return instance;
        }

        case lifetime_kind::transient: {
            try {
                return desc.factory(*this);
            } catch (const di_error&) {
                throw;
            } catch (const std::exception& e) {
                throw resolution_error(desc.component_type, e);
            }
        }
    }

    return nullptr; // Unreachable
}

// ---------------------------------------------------------------
// Resolution (non-template core)
// ---------------------------------------------------------------

std::shared_ptr<void> resolver::resolve_any_impl(
        std::type_index type, const std::string& key) {
    std::size_t idx = 0;
    const descriptor* desc = impl_->find_last(type, key, idx);
    if (!desc) {
        return nullptr;
    }
    return resolve_by_index(idx);
}

std::shared_ptr<void> resolver::resolve_strict_impl(
        std::type_index type, const std::string& key) {
    const auto* indices = impl_->find_all_indices(type, key);
    if (!indices || indices->empty()) {
        return nullptr;
    }
    if (indices->size() > 1) {
        if (key.empty()) {
            throw ambiguous_component(type);
        } else {
            throw ambiguous_component(type, key);
        }
    }
    return resolve_by_index(indices->front());
}

std::vector<std::shared_ptr<void>> resolver::resolve_all_impl(
        std::type_index type, const std::string& key) {
    const auto* indices = impl_->find_all_indices(type, key);
    if (!indices) return {};

    std::vector<std::shared_ptr<void>> results;
    results.reserve(indices->size());
    for (auto idx : *indices) {
        results.push_back(resolve_by_index(idx));
    }
    return results;
}

} // namespace librtdi
