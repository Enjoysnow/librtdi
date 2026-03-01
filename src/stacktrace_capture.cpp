#include "librtdi/descriptor.hpp"

#include <any>

#ifdef LIBRTDI_HAS_STACKTRACE
#include <boost/stacktrace.hpp>
#endif

namespace librtdi::internal {

std::any capture_stacktrace() {
#ifdef LIBRTDI_HAS_STACKTRACE
    return std::any(boost::stacktrace::stacktrace());
#else
    return {};
#endif
}

} // namespace librtdi::internal
