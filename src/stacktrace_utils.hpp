#pragma once

// Internal helper for stacktrace capture and formatting.
// This header is NOT installed — it is only used by the library's .cpp files.

#include "librtdi/descriptor.hpp"
#include "librtdi/exceptions.hpp"

#include <any>
#include <string>
#include <sstream>

#ifdef LIBRTDI_HAS_STACKTRACE
#include <boost/stacktrace.hpp>
#endif

namespace librtdi::internal {

// capture_stacktrace() is declared in descriptor.hpp (public header)
// and implemented in stacktrace_capture.cpp.  No inline definition here.

/// Format a stacktrace stored in a std::any into a human-readable string.
/// Returns an empty string if the any is empty or stacktrace support is
/// disabled.
inline std::string format_stacktrace(const std::any& st) {
#ifdef LIBRTDI_HAS_STACKTRACE
    if (st.has_value()) {
        try {
            const auto& trace =
                std::any_cast<const boost::stacktrace::stacktrace&>(st);
            if (trace.size() > 0) {
                std::ostringstream oss;
                oss << trace;
                return oss.str();
            }
        } catch (const std::bad_any_cast&) {
            // Defensive — should never happen
        }
    }
#else
    (void)st;
#endif
    return {};
}

/// Format one descriptor's registration trace for diagnostic output.
/// Returns a block like:
///   "Registration stacktrace for MyType [impl: Impl] (called via add_singleton):\n  #0 ...\n"
/// or an empty string if no stacktrace is available.
inline std::string format_registration_trace(const descriptor& desc) {
    std::string trace = format_stacktrace(desc.registration_stacktrace);
    if (trace.empty()) return {};

    std::string header = "Registration stacktrace for "
                         + demangle(desc.component_type);
    if (desc.impl_type.has_value()) {
        header += " [impl: " + demangle(desc.impl_type.value()) + "]";
    }
    if (!desc.api_name.empty()) {
        header += " (called via " + desc.api_name + ")";
    }
    return header + ":\n" + trace;
}

} // namespace librtdi::internal
