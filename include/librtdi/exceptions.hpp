#pragma once

#include "export.hpp"

#include <stdexcept>
#include <optional>
#include <string>
#include <string_view>
#include <source_location>
#include <typeindex>
#include <vector>

namespace librtdi {

namespace internal {
/// Demangle a type_index to human-readable name (GCC/Clang ABI-based).
LIBRTDI_EXPORT std::string demangle(std::type_index type);
} // namespace internal

class LIBRTDI_EXPORT di_error : public std::runtime_error {
public:
    explicit di_error(const std::string& message,
                      std::source_location loc = std::source_location::current());

    const std::source_location& location() const noexcept { return location_; }

private:
    std::source_location location_;

    static std::string format_message(const std::string& msg,
                                      const std::source_location& loc);
};

class LIBRTDI_EXPORT not_found : public di_error {
public:
    explicit not_found(std::type_index type,
                       std::source_location loc = std::source_location::current());

    not_found(std::type_index type, std::string_view key,
              std::source_location loc = std::source_location::current());

    /// Construct with an additional diagnostic hint (appended to the message).
    not_found(std::type_index type, std::string_view key,
              std::string_view hint,
              std::source_location loc = std::source_location::current());

    std::type_index component_type() const noexcept { return component_type_; }

private:
    std::type_index component_type_;
};

class LIBRTDI_EXPORT cyclic_dependency : public di_error {
public:
    explicit cyclic_dependency(const std::vector<std::type_index>& cycle,
                               std::source_location loc = std::source_location::current());

    const std::vector<std::type_index>& cycle() const noexcept { return cycle_; }

private:
    std::vector<std::type_index> cycle_;
    static std::string build_message(const std::vector<std::type_index>& cycle);
};

class LIBRTDI_EXPORT lifetime_mismatch : public di_error {
public:
    lifetime_mismatch(std::type_index consumer, std::string_view consumer_lifetime,
                      std::type_index dependency, std::string_view dependency_lifetime,
                      std::optional<std::type_index> consumer_impl = std::nullopt,
                      std::source_location loc = std::source_location::current());

    std::type_index consumer() const noexcept { return consumer_; }
    std::type_index dependency() const noexcept { return dependency_; }

private:
    std::type_index consumer_;
    std::type_index dependency_;

    static std::string build_message(std::type_index consumer, std::string_view consumer_lt,
                                     std::type_index dependency, std::string_view dep_lt,
                                     std::optional<std::type_index> consumer_impl);
};

class LIBRTDI_EXPORT duplicate_registration : public di_error {
public:
    explicit duplicate_registration(std::type_index type,
                                    std::source_location loc = std::source_location::current());

    duplicate_registration(std::type_index type, std::string_view key,
                           std::source_location loc = std::source_location::current());

    std::type_index component_type() const noexcept { return component_type_; }

private:
    std::type_index component_type_;
};

class LIBRTDI_EXPORT resolution_error : public di_error {
public:
    resolution_error(std::type_index type, const std::exception& inner,
                     std::source_location loc = std::source_location::current());

    /// Overload that includes the registration location of the failing component.
    resolution_error(std::type_index type, const std::exception& inner,
                     std::source_location registration_loc,
                     std::source_location loc);

    std::type_index component_type() const noexcept { return component_type_; }

private:
    std::type_index component_type_;
};

} // namespace librtdi
