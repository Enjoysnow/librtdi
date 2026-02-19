#pragma once

#include "descriptor.hpp"
#include "exceptions.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

namespace librtdi {

class scope;

class resolver : public std::enable_shared_from_this<resolver> {
public:
    ~resolver();

    resolver(const resolver&) = delete;
    resolver& operator=(const resolver&) = delete;

    // ---------------------------------------------------------------
    // Non-keyed resolution
    // ---------------------------------------------------------------

    /// resolve a component; throws not_found if not registered,
    /// ambiguous_component if multiple registrations exist.
    template <typename T>
    std::shared_ptr<T> resolve() {
        auto ptr = resolve_strict_impl(typeid(T), std::string{});
        if (!ptr) {
            throw not_found(typeid(T));
        }
        return std::static_pointer_cast<T>(ptr);
    }

    /// resolve a component; returns nullptr if not registered,
    /// throws ambiguous_component if multiple registrations exist.
    template <typename T>
    std::shared_ptr<T> try_resolve() {
        return std::static_pointer_cast<T>(
            resolve_strict_impl(typeid(T), std::string{}));
    }

    /// resolve the last registered implementation (last-wins).
    /// Throws not_found if not registered.
    template <typename T>
    std::shared_ptr<T> resolve_any() {
        auto ptr = resolve_any_impl(typeid(T), std::string{});
        if (!ptr) {
            throw not_found(typeid(T));
        }
        return std::static_pointer_cast<T>(ptr);
    }

    /// resolve the last registered implementation; returns nullptr if not registered.
    template <typename T>
    std::shared_ptr<T> try_resolve_any() {
        auto ptr = resolve_any_impl(typeid(T), std::string{});
        return ptr ? std::static_pointer_cast<T>(ptr) : nullptr;
    }

    /// resolve all implementations registered for T (non-keyed).
    template <typename T>
    std::vector<std::shared_ptr<T>> resolve_all() {
        auto raw = resolve_all_impl(typeid(T), std::string{});
        std::vector<std::shared_ptr<T>> result;
        result.reserve(raw.size());
        for (auto& r : raw) {
            result.push_back(std::static_pointer_cast<T>(r));
        }
        return result;
    }

    // ---------------------------------------------------------------
    // Keyed resolution
    // ---------------------------------------------------------------

    /// resolve a keyed component; throws not_found if not found,
    /// ambiguous_component if multiple registrations exist for that key.
    template <typename T>
    std::shared_ptr<T> resolve(std::string_view key) {
        auto ptr = resolve_strict_impl(typeid(T), std::string(key));
        if (!ptr) {
            throw not_found(typeid(T), key);
        }
        return std::static_pointer_cast<T>(ptr);
    }

    /// resolve a keyed component; returns nullptr if not found,
    /// throws ambiguous_component if multiple registrations exist.
    template <typename T>
    std::shared_ptr<T> try_resolve(std::string_view key) {
        return std::static_pointer_cast<T>(
            resolve_strict_impl(typeid(T), std::string(key)));
    }

    /// resolve the last registered keyed implementation (last-wins).
    template <typename T>
    std::shared_ptr<T> resolve_any(std::string_view key) {
        auto ptr = resolve_any_impl(typeid(T), std::string(key));
        if (!ptr) {
            throw not_found(typeid(T), key);
        }
        return std::static_pointer_cast<T>(ptr);
    }

    /// resolve the last registered keyed implementation; returns nullptr if not found.
    template <typename T>
    std::shared_ptr<T> try_resolve_any(std::string_view key) {
        auto ptr = resolve_any_impl(typeid(T), std::string(key));
        return ptr ? std::static_pointer_cast<T>(ptr) : nullptr;
    }

    /// resolve all implementations registered for T with the given key.
    template <typename T>
    std::vector<std::shared_ptr<T>> resolve_all(std::string_view key) {
        auto raw = resolve_all_impl(typeid(T), std::string(key));
        std::vector<std::shared_ptr<T>> result;
        result.reserve(raw.size());
        for (auto& r : raw) {
            result.push_back(std::static_pointer_cast<T>(r));
        }
        return result;
    }

    // ---------------------------------------------------------------
    // scope management
    // ---------------------------------------------------------------

    std::unique_ptr<scope> create_scope();

    bool is_root() const noexcept;

    /// resolve a specific descriptor by its internal index.
    std::shared_ptr<void> resolve_by_index(std::size_t idx);

private:
    friend class registry;
    friend class scope;

    struct impl;

    static std::shared_ptr<resolver> create(std::vector<descriptor> descriptors);

    explicit resolver(std::unique_ptr<impl> impl);

    resolver(std::shared_ptr<impl> shared_impl, bool is_scoped);

    std::shared_ptr<void> resolve_strict_impl(std::type_index type, const std::string& key);
    std::shared_ptr<void> resolve_any_impl(std::type_index type, const std::string& key);
    std::vector<std::shared_ptr<void>> resolve_all_impl(std::type_index type, const std::string& key);

    std::shared_ptr<impl> impl_;
    bool is_scoped_ = false;
    struct scoped_cache;
    std::unique_ptr<scoped_cache> scoped_cache_;
};

} // namespace librtdi
