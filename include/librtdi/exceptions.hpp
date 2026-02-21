#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <source_location>
#include <typeindex>
#include <vector>

namespace librtdi {

class di_error : public std::runtime_error {
public:
    explicit di_error(const std::string& message,
                      std::source_location loc = std::source_location::current());

    const std::source_location& location() const noexcept { return location_; }

private:
    std::source_location location_;

    static std::string format_message(const std::string& msg,
                                      const std::source_location& loc);
};

class not_found : public di_error {
public:
    explicit not_found(std::type_index type,
                       std::source_location loc = std::source_location::current());

    not_found(std::type_index type, std::string_view key,
              std::source_location loc = std::source_location::current());

    std::type_index component_type() const noexcept { return component_type_; }

private:
    std::type_index component_type_;
};

// Thrown when a cyclic dependency is detected
class cyclic_dependency : public di_error {
public:
    explicit cyclic_dependency(const std::vector<std::type_index>& cycle,
                               std::source_location loc = std::source_location::current());

    const std::vector<std::type_index>& cycle() const noexcept { return cycle_; }

private:
    std::vector<std::type_index> cycle_;

    static std::string build_message(const std::vector<std::type_index>& cycle);
};

// Thrown when lifetime rules are violated (e.g. Singleton depends on Scoped)
class lifetime_mismatch : public di_error {
public:
    lifetime_mismatch(std::type_index consumer, std::string_view consumer_lifetime,
                      std::type_index dependency, std::string_view dependency_lifetime,
                      std::source_location loc = std::source_location::current());

    std::type_index consumer() const noexcept { return consumer_; }
    std::type_index dependency() const noexcept { return dependency_; }

private:
    std::type_index consumer_;
    std::type_index dependency_;

    static std::string build_message(std::type_index consumer, std::string_view consumer_lt,
                                     std::type_index dependency, std::string_view dep_lt);
};

// Thrown when trying to resolve a Scoped component from the root resolver
class no_active_scope : public di_error {
public:
    explicit no_active_scope(std::type_index type,
                             std::source_location loc = std::source_location::current());

    std::type_index component_type() const noexcept { return component_type_; }

private:
    std::type_index component_type_;
};

// Thrown when registration_policy::single is violated
class duplicate_registration : public di_error {
public:
    explicit duplicate_registration(std::type_index type,
                                    std::source_location loc = std::source_location::current());

    duplicate_registration(std::type_index type, std::string_view key,
                           std::source_location loc = std::source_location::current());

    std::type_index component_type() const noexcept { return component_type_; }

private:
    std::type_index component_type_;
};

// Thrown when a factory throws during resolution, wrapping the original exception
class resolution_error : public di_error {
public:
    resolution_error(std::type_index type, const std::exception& inner,
                     std::source_location loc = std::source_location::current());

    std::type_index component_type() const noexcept { return component_type_; }

private:
    std::type_index component_type_;
};

// Thrown when resolve<T>() finds multiple registrations for T
class ambiguous_component : public di_error {
public:
    explicit ambiguous_component(std::type_index type,
                                 std::source_location loc = std::source_location::current());

    ambiguous_component(std::type_index type, std::string_view key,
                        std::source_location loc = std::source_location::current());

    std::type_index component_type() const noexcept { return component_type_; }

private:
    std::type_index component_type_;
};

} // namespace librtdi
