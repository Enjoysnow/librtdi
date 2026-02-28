#include "librtdi/descriptor.hpp"
#include "librtdi/exceptions.hpp"
#include "stacktrace_utils.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <source_location>
#include <tuple>
#include <typeindex>
#include <vector>

namespace librtdi {

// slot_key = (type, key, lifetime, is_collection)
using slot_key = std::tuple<std::type_index, std::string,
                            lifetime_kind, bool>;

namespace {

// Build an index from slot_key → list of descriptor indices
auto build_slot_index(const std::vector<descriptor>& descriptors) {
    std::map<slot_key, std::vector<std::size_t>> idx;
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        auto& d = descriptors[i];
        idx[{d.component_type, d.key, d.lifetime, d.is_collection}]
            .push_back(i);
    }
    return idx;
}

// ------------------------------------------------------------------
// Check that every dependency_info has a matching slot
// ------------------------------------------------------------------
void check_missing_dependencies(
        const std::vector<descriptor>& descriptors,
        const std::map<slot_key, std::vector<std::size_t>>& slot_idx,
        const build_options& options,
        std::source_location loc) {
    for (auto& desc : descriptors) {
        for (auto& dep : desc.dependencies) {
            // Collection dependencies are implicitly optional when
            // allow_empty_collections is true (the industry default).
            // The resolver's get_all / create_all already return empty
            // vectors for unregistered collections, so skipping them
            // here keeps build-time validation consistent with runtime.
            if (dep.is_collection && options.allow_empty_collections)
                continue;
            auto needed_lt = dep.is_transient ? lifetime_kind::transient
                                              : lifetime_kind::singleton;
            slot_key needed{dep.type, "" /* deps always empty key */,
                            needed_lt, dep.is_collection};

            auto it = slot_idx.find(needed);
            if (it == slot_idx.end() || it->second.empty()) {
                // Build a diagnostic hint telling the user which consumer
                // requires this missing dependency.
                std::string hint = "required by "
                    + internal::demangle(desc.component_type);
                if (desc.impl_type.has_value()) {
                    hint += " [impl: "
                        + internal::demangle(desc.impl_type.value()) + "]";
                }
                hint += " (" + std::string(
                    desc.lifetime == lifetime_kind::singleton
                        ? "singleton" : "transient") + ")";
                if (!desc.registration_location.file_name()[0]) {
                    // no registration location recorded (e.g. forward-generated)
                } else {
                    hint += " registered at "
                        + std::string(desc.registration_location.file_name())
                        + ":" + std::to_string(desc.registration_location.line());
                }
                auto ex = not_found(dep.type, std::string_view{}, hint, loc);
                ex.set_diagnostic_detail(
                    internal::format_registration_trace(desc));
                throw ex;
            }
        }
    }
}

// ------------------------------------------------------------------
// Lifetime validation (captive dependency check)
// ------------------------------------------------------------------
void check_lifetime_rules(const std::vector<descriptor>& descriptors,
                          std::source_location loc) {
    for (auto& desc : descriptors) {
        if (desc.lifetime != lifetime_kind::singleton) continue;

        for (auto& dep : desc.dependencies) {
            if (dep.is_transient && !dep.is_collection) {
                // Singleton depending on a transient single instance
                // is a captive dependency — the singleton captures a fresh
                // instance once and never gets a new one.
                auto ex = lifetime_mismatch(desc.component_type, "singleton",
                                        dep.type, "transient",
                                        desc.impl_type, loc);
                ex.set_diagnostic_detail(
                    internal::format_registration_trace(desc));
                throw ex;
            }
        }
    }
}

// ------------------------------------------------------------------
// Cycle detection (DFS on component dependency graph)
// ------------------------------------------------------------------
enum class visit_state { unvisited, in_progress, done };

void dfs(std::type_index node, bool is_collection, bool is_transient,
         const std::map<slot_key, std::vector<std::size_t>>& slot_idx,
         const std::vector<descriptor>& descriptors,
         std::map<std::type_index, visit_state>& states,
         std::vector<std::type_index>& path,
         std::source_location loc) {

    // For cycle detection we key on type only (regardless of slot variant)
    auto& state = states[node];
    if (state == visit_state::done) return;
    if (state == visit_state::in_progress) {
        // Build cycle path from where the node first appears
        auto it = std::find(path.begin(), path.end(), node);
        std::vector<std::type_index> cycle(it, path.end());
        cycle.push_back(node);
        auto ex = cyclic_dependency(cycle, loc);
        // Attach registration stacktraces for all types in the cycle
        std::string detail;
        for (auto& ti : cycle) {
            // Find a descriptor for this type to get its stacktrace
            for (auto& d : descriptors) {
                if (d.component_type == ti) {
                    std::string trace = internal::format_registration_trace(d);
                    if (!trace.empty()) {
                        if (!detail.empty()) detail += "\n";
                        detail += trace;
                    }
                    break;
                }
            }
        }
        if (!detail.empty()) ex.set_diagnostic_detail(detail);
        throw ex;
    }

    state = visit_state::in_progress;
    path.push_back(node);

    // Find all descriptors that provide this type
    auto needed_lt = is_transient ? lifetime_kind::transient
                                  : lifetime_kind::singleton;
    slot_key sk{node, "", needed_lt, is_collection};
    auto it = slot_idx.find(sk);
    if (it != slot_idx.end()) {
        for (auto idx : it->second) {
            auto& dep_desc = descriptors[idx];
            for (auto& dep : dep_desc.dependencies) {
                dfs(dep.type, dep.is_collection, dep.is_transient,
                    slot_idx, descriptors, states, path, loc);
            }
        }
    }

    path.pop_back();
    state = visit_state::done;
}

void check_cycles(const std::vector<descriptor>& descriptors,
                  const std::map<slot_key, std::vector<std::size_t>>& slot_idx,
                  std::source_location loc) {
    std::map<std::type_index, visit_state> states;
    std::vector<std::type_index> path;

    for (auto& desc : descriptors) {
        if (states[desc.component_type] == visit_state::unvisited) {
            dfs(desc.component_type, desc.is_collection,
                desc.lifetime == lifetime_kind::transient,
                slot_idx, descriptors, states, path, loc);
        }
    }
}

} // anonymous namespace

// ------------------------------------------------------------------
// Public entry point called by registry::build
// ------------------------------------------------------------------
void validate_descriptors(const std::vector<descriptor>& descriptors,
                          const build_options& options,
                          std::source_location loc) {
    auto slot_idx = build_slot_index(descriptors);

    check_missing_dependencies(descriptors, slot_idx, options, loc);

    if (options.validate_lifetimes) {
        check_lifetime_rules(descriptors, loc);
    }

    if (options.detect_cycles) {
        check_cycles(descriptors, slot_idx, loc);
    }
}

} // namespace librtdi
