#pragma once

#include "export.hpp"
#include "descriptor.hpp"
#include "exceptions.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

namespace librtdi {

class LIBRTDI_EXPORT resolver {
public:
    ~resolver();

    resolver(const resolver&) = delete;
    resolver& operator=(const resolver&) = delete;

    // ---------------------------------------------------------------
    // Non-keyed singleton resolution
    // ---------------------------------------------------------------

    /// Get a singleton by interface.  Throws not_found if not registered.
    template <typename T>
    T& get() {
        void* p = get_singleton_impl(typeid(T), std::string{});
        if (!p) throw not_found(typeid(T), std::string_view{},
                                slot_hint(typeid(T), {}, "get<T>()"));
        return *static_cast<T*>(p);
    }

    /// Get a singleton; returns nullptr if not registered.
    template <typename T>
    T* try_get() {
        return static_cast<T*>(get_singleton_impl(typeid(T), std::string{}));
    }

    /// Create a new transient instance.  Throws not_found if not registered.
    template <typename T>
    std::unique_ptr<T> create() {
        auto ep = create_transient_impl(typeid(T), std::string{});
        if (!ep) throw not_found(typeid(T), std::string_view{},
                                 slot_hint(typeid(T), {}, "create<T>()"));
        return std::unique_ptr<T>(static_cast<T*>(ep.release()));
    }

    /// Create a new transient instance; returns empty ptr if not registered.
    template <typename T>
    std::unique_ptr<T> try_create() {
        auto ep = create_transient_impl(typeid(T), std::string{});
        if (!ep) return nullptr;
        return std::unique_ptr<T>(static_cast<T*>(ep.release()));
    }

    /// Get all singleton collection items for T.
    template <typename T>
    std::vector<T*> get_all() {
        auto raw = get_collection_impl(typeid(T), std::string{});
        std::vector<T*> result;
        result.reserve(raw.size());
        for (void* p : raw) result.push_back(static_cast<T*>(p));
        return result;
    }

    /// Create all transient collection items for T.
    template <typename T>
    std::vector<std::unique_ptr<T>> create_all() {
        auto raw = create_collection_impl(typeid(T), std::string{});
        std::vector<std::unique_ptr<T>> result;
        result.reserve(raw.size());
        for (auto& ep : raw) {
            result.push_back(std::unique_ptr<T>(static_cast<T*>(ep.release())));
        }
        return result;
    }

    // ---------------------------------------------------------------
    // Keyed singleton resolution
    // ---------------------------------------------------------------

    /// Get a keyed singleton by interface.  Throws not_found if not registered.
    template <typename T>
    T& get(std::string_view key) {
        void* p = get_singleton_impl(typeid(T), std::string(key));
        if (!p) throw not_found(typeid(T), key,
                                slot_hint(typeid(T), std::string(key), "get<T>(key)"));
        return *static_cast<T*>(p);
    }

    /// Get a keyed singleton; returns nullptr if not registered.
    template <typename T>
    T* try_get(std::string_view key) {
        return static_cast<T*>(get_singleton_impl(typeid(T), std::string(key)));
    }

    /// Create a new keyed transient instance.  Throws not_found if not registered.
    template <typename T>
    std::unique_ptr<T> create(std::string_view key) {
        auto ep = create_transient_impl(typeid(T), std::string(key));
        if (!ep) throw not_found(typeid(T), key,
                                 slot_hint(typeid(T), std::string(key), "create<T>(key)"));
        return std::unique_ptr<T>(static_cast<T*>(ep.release()));
    }

    /// Create a new keyed transient instance; returns empty ptr if not registered.
    template <typename T>
    std::unique_ptr<T> try_create(std::string_view key) {
        auto ep = create_transient_impl(typeid(T), std::string(key));
        if (!ep) return nullptr;
        return std::unique_ptr<T>(static_cast<T*>(ep.release()));
    }

    /// Get all keyed singleton collection items for T.
    template <typename T>
    std::vector<T*> get_all(std::string_view key) {
        auto raw = get_collection_impl(typeid(T), std::string(key));
        std::vector<T*> result;
        result.reserve(raw.size());
        for (void* p : raw) result.push_back(static_cast<T*>(p));
        return result;
    }

    /// Create all keyed transient collection items for T.
    template <typename T>
    std::vector<std::unique_ptr<T>> create_all(std::string_view key) {
        auto raw = create_collection_impl(typeid(T), std::string(key));
        std::vector<std::unique_ptr<T>> result;
        result.reserve(raw.size());
        for (auto& ep : raw) {
            result.push_back(std::unique_ptr<T>(static_cast<T*>(ep.release())));
        }
        return result;
    }

    // ---------------------------------------------------------------
    // Internal: resolve a descriptor by index (used by forward)
    // ---------------------------------------------------------------

    void* resolve_singleton_by_index(std::size_t idx);
    erased_ptr resolve_transient_by_index(std::size_t idx);

private:
    friend class registry;

    struct impl;

    static std::shared_ptr<resolver> create(std::vector<descriptor> descriptors);

    explicit resolver(std::unique_ptr<impl> impl);

    // Non-template core implementations
    void* get_singleton_impl(std::type_index type, const std::string& key);
    erased_ptr create_transient_impl(std::type_index type, const std::string& key);
    std::vector<void*> get_collection_impl(std::type_index type, const std::string& key);
    std::vector<erased_ptr> create_collection_impl(std::type_index type, const std::string& key);

    /// Build a diagnostic hint when a type is not found in the expected slot.
    std::string slot_hint(std::type_index type, const std::string& key,
                          const char* attempted_method) const;

    std::shared_ptr<impl> impl_;
};

} // namespace librtdi
