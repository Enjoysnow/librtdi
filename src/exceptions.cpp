#include "librtdi/exceptions.hpp"

#include <cstdlib>
#include <typeindex>
#include <string>
#include <memory>

#if defined(__GNUC__)
// Covers both GCC and Clang (which defines __GNUC__ for compatibility).
// MSVC does not define __GNUC__ and falls through to the plain name() fallback.
#include <cxxabi.h>
#endif

namespace librtdi {

namespace internal {
/// Returns the human-readable (demangled) name of the given type.
///
/// On GCC and Clang, uses abi::__cxa_demangle to produce the C++ type name.
/// On MSVC, typeid(...).name() is already reasonably readable, so it is
/// returned as-is.
static std::string demangle(std::type_index type) {
#if defined(__GNUC__)
    int status = 0;
    std::unique_ptr<char, decltype(&std::free)> demangled(
        abi::__cxa_demangle(type.name(), nullptr, nullptr, &status),
        &std::free);
    if (status == 0 && demangled) {
        return std::string(demangled.get());
    }
#endif
    // MSVC: type.name() already contains a decorated but readable C++ name.
    // Fallback for any platform where demangling is unavailable.
    return std::string(type.name());
}

} // namespace internal

std::string di_error::format_message(const std::string& msg,
                                     const std::source_location& loc) {
    return msg + " [at " + loc.file_name() + ":"
           + std::to_string(loc.line()) + "]";
}

di_error::di_error(const std::string& message, std::source_location loc)
    : std::runtime_error(format_message(message, loc))
    , location_(loc)
{}

not_found::not_found(std::type_index type, std::source_location loc)
    : di_error("Component not found: " + internal::demangle(type), loc)
    , component_type_(type)
{}

not_found::not_found(std::type_index type, std::string_view key,
                     std::source_location loc)
    : di_error("Component not found: " + internal::demangle(type)
               + " (key=\"" + std::string(key) + "\")", loc)
    , component_type_(type)
{}

std::string cyclic_dependency::build_message(const std::vector<std::type_index>& cycle) {
    std::string msg = "Cyclic dependency detected: ";
    for (std::size_t i = 0; i < cycle.size(); ++i) {
        if (i > 0) msg += " -> ";
        msg += internal::demangle(cycle[i]);
    }
    if (!cycle.empty()) {
        msg += " -> " + internal::demangle(cycle.front());
    }
    return msg;
}

cyclic_dependency::cyclic_dependency(const std::vector<std::type_index>& cycle,
                                     std::source_location loc)
    : di_error(build_message(cycle), loc)
    , cycle_(cycle)
{}

std::string lifetime_mismatch::build_message(std::type_index consumer,
                                             std::string_view consumer_lt,
                                             std::type_index dependency,
                                             std::string_view dep_lt) {
    return "lifetime_kind mismatch: " + internal::demangle(consumer)
           + " (" + std::string(consumer_lt) + ") depends on "
           + internal::demangle(dependency) + " (" + std::string(dep_lt) + ")";
}

lifetime_mismatch::lifetime_mismatch(std::type_index consumer,
                                     std::string_view consumer_lifetime,
                                     std::type_index dependency,
                                     std::string_view dependency_lifetime,
                                     std::source_location loc)
    : di_error(build_message(consumer, consumer_lifetime,
                             dependency, dependency_lifetime), loc)
    , consumer_(consumer)
    , dependency_(dependency)
{}

no_active_scope::no_active_scope(std::type_index type, std::source_location loc)
    : di_error("Cannot resolve scoped component from root resolver: "
               + internal::demangle(type), loc)
    , component_type_(type)
{}

duplicate_registration::duplicate_registration(std::type_index type,
                                               std::source_location loc)
    : di_error("Duplicate registration for: " + internal::demangle(type), loc)
    , component_type_(type)
{}

duplicate_registration::duplicate_registration(std::type_index type,
                                               std::string_view key,
                                               std::source_location loc)
    : di_error("Duplicate registration for: " + internal::demangle(type)
               + " (key=\"" + std::string(key) + "\")", loc)
    , component_type_(type)
{}

resolution_error::resolution_error(std::type_index type,
                                   const std::exception& inner,
                                   std::source_location loc)
    : di_error("Failed to resolve component " + internal::demangle(type)
               + ": " + inner.what(), loc)
    , component_type_(type)
{}

ambiguous_component::ambiguous_component(std::type_index type,
                                         std::source_location loc)
    : di_error("Ambiguous component resolution: " + internal::demangle(type)
               + " has multiple registrations", loc)
    , component_type_(type)
{}

ambiguous_component::ambiguous_component(std::type_index type,
                                         std::string_view key,
                                         std::source_location loc)
    : di_error("Ambiguous component resolution: " + internal::demangle(type)
               + " (key=\"" + std::string(key)
               + "\") has multiple registrations", loc)
    , component_type_(type)
{}

} // namespace librtdi
