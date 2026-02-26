#pragma once

#include <type_traits>
#include <utility>

namespace librtdi {

// ---------------------------------------------------------------
// erased_ptr — type-erased owning pointer
// ---------------------------------------------------------------

/// Type-erased owning pointer.  Wraps a raw `void*` with a custom deleter.
/// `unique_ptr<void, D>` is ill-formed because `void` is incomplete, so we
/// use a thin RAII wrapper instead.
struct erased_ptr {
    void* ptr = nullptr;
    void (*deleter)(void*) = nullptr;

    erased_ptr() = default;
    erased_ptr(void* p, void (*d)(void*)) noexcept : ptr(p), deleter(d) {}

    erased_ptr(erased_ptr&& o) noexcept : ptr(o.ptr), deleter(o.deleter) {
        o.ptr = nullptr;
        o.deleter = nullptr;
    }
    erased_ptr& operator=(erased_ptr&& o) noexcept {
        if (this != &o) {
            reset();
            ptr = o.ptr;
            deleter = o.deleter;
            o.ptr = nullptr;
            o.deleter = nullptr;
        }
        return *this;
    }

    erased_ptr(const erased_ptr&) = delete;
    erased_ptr& operator=(const erased_ptr&) = delete;

    ~erased_ptr() { reset(); }

    void reset() noexcept {
        if (ptr && deleter) deleter(ptr);
        ptr = nullptr;
        deleter = nullptr;
    }

    void* get() const noexcept { return ptr; }
    explicit operator bool() const noexcept { return ptr != nullptr; }

    /// Release ownership — caller must arrange deletion.
    void* release() noexcept {
        void* p = ptr;
        ptr = nullptr;
        deleter = nullptr;
        return p;
    }
};

/// Create an erased_ptr that owns a `new T(args...)`.
template <typename T, typename... Args>
erased_ptr make_erased(Args&&... args) {
    return erased_ptr(
        static_cast<void*>(new T(std::forward<Args>(args)...)),
        [](void* p) { delete static_cast<T*>(p); }
    );
}

/// Create an erased_ptr that owns a `new TImpl(args...)`, storing the pointer
/// as `TInterface*` in the void*.  This ensures that `static_cast<TInterface*>(void*)`
/// round-trips correctly even under multiple or virtual inheritance.
/// Requires TInterface to have a virtual destructor (when TInterface != TImpl)
/// so that `delete static_cast<TInterface*>(p)` correctly destroys the full object.
template <typename TInterface, typename TImpl, typename... Args>
    requires std::is_base_of_v<TInterface, TImpl>
erased_ptr make_erased_as(Args&&... args) {
    static_assert(std::is_same_v<TInterface, TImpl>
               || std::has_virtual_destructor_v<TInterface>,
        "TInterface must have a virtual destructor when TInterface != TImpl "
        "(required for correct polymorphic deletion via base pointer)");
    auto* impl = new TImpl(std::forward<Args>(args)...);
    return erased_ptr(
        static_cast<void*>(static_cast<TInterface*>(impl)),
        [](void* p) { delete static_cast<TInterface*>(p); }
    );
}

} // namespace librtdi
