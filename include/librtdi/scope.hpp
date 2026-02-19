#pragma once

#include "resolver.hpp"

#include <memory>

namespace librtdi {

/// RAII scope object. When destroyed, all scoped components resolved
/// within this scope are released (shared_ptr refcount drops).
class scope {
public:
    ~scope();

    scope(const scope&) = delete;
    scope& operator=(const scope&) = delete;
    scope(scope&&) noexcept;
    scope& operator=(scope&&) noexcept;

    /// Get the scoped resolver associated with this scope.
    resolver& get_resolver() noexcept;

private:
    friend class resolver;
    explicit scope(std::shared_ptr<resolver> scoped_resolver);

    std::shared_ptr<resolver> resolver_;
};

} // namespace librtdi
