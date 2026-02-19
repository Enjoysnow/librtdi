#include "librtdi/scope.hpp"
#include "librtdi/resolver.hpp"

namespace librtdi {

scope::scope(std::shared_ptr<resolver> scoped_resolver)
    : resolver_(std::move(scoped_resolver))
{}

scope::~scope() = default;

scope::scope(scope&&) noexcept = default;
scope& scope::operator=(scope&&) noexcept = default;

resolver& scope::get_resolver() noexcept {
    return *resolver_;
}

} // namespace librtdi
