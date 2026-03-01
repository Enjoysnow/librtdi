#include "librtdi/exceptions.hpp"

#include <cstdlib>
#include <optional>
#include <typeindex>
#include <string>
#include <memory>

#if defined(__GNUC__)
#include <cxxabi.h>
#endif

namespace librtdi {

namespace internal {

std::string demangle(std::type_index type) {
#if defined(__GNUC__)
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

void di_error::set_diagnostic_detail(std::string detail) {
    diagnostic_detail_ = std::move(detail);
}

void di_error::append_resolution_context(const std::string& component_info) {
    if (!resolution_context_.empty()) {
        resolution_context_ += " -> ";
    }
    resolution_context_ += component_info;
    cached_what_.clear();
}

const char* di_error::what() const noexcept {
    if (resolution_context_.empty()) {
        return std::runtime_error::what();
    }
    if (cached_what_.empty()) {
        try {
            cached_what_ = std::string(std::runtime_error::what())
                           + " (while resolving " + resolution_context_ + ")";
        } catch (...) {
            return std::runtime_error::what();
        }
    }
    return cached_what_.c_str();
}

std::string di_error::full_diagnostic() const {
    if (diagnostic_detail_.empty()) {
        return what();
    }
    return std::string(what()) + "\n" + diagnostic_detail_;
}

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

not_found::not_found(std::type_index type, std::string_view key,
                     std::string_view hint, std::source_location loc)
    : di_error([&]() {
          std::string msg = "Component not found: " + internal::demangle(type);
          if (!key.empty())
              msg += " (key=\"" + std::string(key) + "\")";
          if (!hint.empty())
              msg += "; " + std::string(hint);
          return msg;
      }(), loc)
    , component_type_(type)
{}

std::string cyclic_dependency::build_message(const std::vector<std::type_index>& cycle) {
    std::string msg = "Cyclic dependency detected: ";
    for (std::size_t i = 0; i < cycle.size(); ++i) {
        if (i > 0) msg += " -> ";
        msg += internal::demangle(cycle[i]);
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
                                             std::string_view dep_lt,
                                             std::optional<std::type_index> consumer_impl) {
    std::string msg = "lifetime_kind mismatch: " + internal::demangle(consumer);
    if (consumer_impl.has_value()) {
        msg += " [impl: " + internal::demangle(consumer_impl.value()) + "]";
    }
    msg += " (" + std::string(consumer_lt) + ") depends on "
           + internal::demangle(dependency) + " (" + std::string(dep_lt) + ")";
    return msg;
}

lifetime_mismatch::lifetime_mismatch(std::type_index consumer,
                                     std::string_view consumer_lifetime,
                                     std::type_index dependency,
                                     std::string_view dependency_lifetime,
                                     std::optional<std::type_index> consumer_impl,
                                     std::source_location loc)
    : di_error(build_message(consumer, consumer_lifetime,
                             dependency, dependency_lifetime, consumer_impl), loc)
    , consumer_(consumer)
    , dependency_(dependency)
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

resolution_error::resolution_error(std::type_index type,
                                   const std::exception& inner,
                                   std::source_location registration_loc,
                                   std::source_location /*loc*/)
    : di_error([&]() {
          std::string msg = "Failed to resolve component " + internal::demangle(type)
                            + ": " + inner.what();
          if (registration_loc.file_name()[0]) {
              msg += " (registered at " + std::string(registration_loc.file_name())
                     + ":" + std::to_string(registration_loc.line()) + ")";
          }
          return msg;
      }(), registration_loc)
    , component_type_(type)
{}



} // namespace librtdi
