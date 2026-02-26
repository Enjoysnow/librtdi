#pragma once

#include "erased_ptr.hpp"

#include <utility>

namespace librtdi {

// ---------------------------------------------------------------
// decorated_ptr<I> — decorator receives this instead of unique_ptr<I>
// ---------------------------------------------------------------

/// Holds a reference to the decorated object plus optional ownership.
/// For singleton descriptors the inner erased_ptr has deleter==nullptr
/// (non-owning), for transient descriptors it owns the object.
template <typename I>
struct decorated_ptr {
    decorated_ptr(I* p, erased_ptr owner) noexcept
        : ptr_(p), owner_(std::move(owner)) {}

    decorated_ptr(decorated_ptr&& o) noexcept
        : ptr_(o.ptr_), owner_(std::move(o.owner_)) { o.ptr_ = nullptr; }

    decorated_ptr& operator=(decorated_ptr&& o) noexcept {
        if (this != &o) {
            ptr_ = o.ptr_;
            owner_ = std::move(o.owner_);
            o.ptr_ = nullptr;
        }
        return *this;
    }

    decorated_ptr(const decorated_ptr&) = delete;
    decorated_ptr& operator=(const decorated_ptr&) = delete;

    I& get() const noexcept { return *ptr_; }
    I* operator->() const noexcept { return ptr_; }
    I& operator*() const noexcept { return *ptr_; }

    /// True when this handle owns the inner object (transient).
    /// False for singleton (the resolver cache owns the instance).
    bool owns() const noexcept { return owner_.deleter != nullptr; }

private:
    I* ptr_ = nullptr;
    erased_ptr owner_;
};

} // namespace librtdi
