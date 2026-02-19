#include "librtdi/exceptions.hpp"

#include <typeindex>
#include <string>
#include <cstdlib>
#include <memory>

#ifdef __GNUC__
#include <cxxabi.h>
#endif

namespace librtdi {

// ---------------------------------------------------------------
// demangle
// ---------------------------------------------------------------

std::string demangle(std::type_index type) {
#ifdef __GNUC__
    int status = 0;
    std::unique_ptr<char, decltype(&std::free)> demangled(
        abi::__cxa_demangle(type.name(), nullptr, nullptr, &status),
        &std::free);
    if (status == 0 && demangled) {
        return std::string(demangled.get());
    }
#endif
    return std::string(type.name());
}

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
    : di_error("Component not found: " + demangle(type), loc)
    , component_type_(type)
{}

not_found::not_found(std::type_index type, std::string_view key,
                     std::source_location loc)
    : di_error("Component not found: " + demangle(type)
               + " (key=\"" + std::string(key) + "\")", loc)
    , component_type_(type)
{}

std::string cyclic_dependency::build_message(const std::vector<std::type_index>& cycle) {
    std::string msg = "Cyclic dependency detected: ";
    for (std::size_t i = 0; i < cycle.size(); ++i) {
        if (i > 0) msg += " -> ";
        msg += demangle(cycle[i]);
    }
    if (!cycle.empty()) {
        msg += " -> " + demangle(cycle.front());
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
    return "lifetime_kind mismatch: " + demangle(consumer)
           + " (" + std::string(consumer_lt) + ") depends on "
           + demangle(dependency) + " (" + std::string(dep_lt) + ")";
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
               + demangle(type), loc)
    , component_type_(type)
{}

duplicate_registration::duplicate_registration(std::type_index type,
                                               std::source_location loc)
    : di_error("Duplicate registration for: " + demangle(type), loc)
    , component_type_(type)
{}

duplicate_registration::duplicate_registration(std::type_index type,
                                               std::string_view key,
                                               std::source_location loc)
    : di_error("Duplicate registration for: " + demangle(type)
               + " (key=\"" + std::string(key) + "\")", loc)
    , component_type_(type)
{}

resolution_error::resolution_error(std::type_index type,
                                   const std::exception& inner,
                                   std::source_location loc)
    : di_error("Failed to resolve component " + demangle(type)
               + ": " + inner.what(), loc)
    , component_type_(type)
{}

ambiguous_component::ambiguous_component(std::type_index type,
                                         std::source_location loc)
    : di_error("Ambiguous component resolution: " + demangle(type)
               + " has multiple registrations", loc)
    , component_type_(type)
{}

ambiguous_component::ambiguous_component(std::type_index type,
                                         std::string_view key,
                                         std::source_location loc)
    : di_error("Ambiguous component resolution: " + demangle(type)
               + " (key=\"" + std::string(key)
               + "\") has multiple registrations", loc)
    , component_type_(type)
{}

} // namespace librtdi
