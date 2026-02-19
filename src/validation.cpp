#include "librtdi/descriptor.hpp"
#include "librtdi/registry.hpp"
#include "librtdi/exceptions.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <typeindex>

namespace librtdi {

// ---------------------------------------------------------------
// Internal validation helpers
// ---------------------------------------------------------------

namespace {

enum class color_kind { white, gray, black };

struct validation_context {
    // Map from type_index to its last NON-KEYED descriptor (for lifetime lookup)
    std::unordered_map<std::type_index, const descriptor*> last_descriptor;
    // All descriptors (keyed and non-keyed)
    std::vector<const descriptor*> all;
    // Adjacency list for dependency graph (built from ALL descriptors)
    std::unordered_map<std::type_index, std::vector<std::type_index>> adj;
    // Types that have at least one non-keyed registration
    std::unordered_set<std::type_index> registered;
    // Count of non-keyed registrations per type (for ambiguity detection)
    std::unordered_map<std::type_index, std::size_t> registration_count;

    void build(const std::vector<descriptor>& descriptors) {
        for (auto& d : descriptors) {
            all.push_back(&d);
            if (d.key.empty()) {
                // Non-keyed registration
                registered.insert(d.component_type);
                registration_count[d.component_type]++;
                last_descriptor[d.component_type] = &d;
            }
            // build adjacency from ALL descriptors' dependency lists
            // (deps<> always resolves non-keyed, so edges target non-keyed types)
            auto& edges = adj[d.component_type];
            for (auto& dep : d.dependencies) {
                edges.push_back(dep);
            }
        }
    }
};

// Check for missing dependencies (all descriptors, keyed and non-keyed)
// deps<> always resolves non-keyed, so check against non-keyed registered set
void check_missing_dependencies(const validation_context& ctx) {
    for (auto* desc : ctx.all) {
        for (auto& dep : desc->dependencies) {
            if (!ctx.registered.contains(dep)) {
                throw not_found(dep);
            }
        }
    }
}

// Check for ambiguous dependencies: if a descriptor's dependency has multiple
// non-keyed registrations, resolve<T>() (used by deps<>) would throw at runtime.
// Catch this early at build time. forward descriptors are exempt for their
// target dependency because they use resolve_any internally.
void check_ambiguous_dependencies(const validation_context& ctx) {
    for (auto* desc : ctx.all) {
        for (auto& dep : desc->dependencies) {
            // forward descriptors resolve their target via resolve_any (not strict resolve),
            // so skip the ambiguity check for the forward target dependency.
            if (desc->forward_target.has_value() && dep == desc->forward_target.value()) {
                continue;
            }
            auto it = ctx.registration_count.find(dep);
            if (it != ctx.registration_count.end() && it->second > 1) {
                throw ambiguous_component(dep);
            }
        }
    }
}

// Check for cyclic dependencies using DFS 3-color marking
void dfs_cycle_detect(std::type_index node,
                    const validation_context& ctx,
                    std::unordered_map<std::type_index, color_kind>& color,
                    std::vector<std::type_index>& path) {
    color[node] = color_kind::gray;
    path.push_back(node);

    auto adj_it = ctx.adj.find(node);
    if (adj_it != ctx.adj.end()) {
        for (auto& dep : adj_it->second) {
            auto color_it = color.find(dep);
            // Safe: check_missing_dependencies runs first and throws not_found for any
            // unregistered dependency, so a missing color entry here means the node is
            // simply unvisited (White) in this DFS traversal.
            color_kind dep_color = (color_it != color.end()) ? color_it->second : color_kind::white;
            if (dep_color == color_kind::gray) {
                // Found a cycle — extract the cycle portion from path
                std::vector<std::type_index> cycle;
                auto it = std::find(path.begin(), path.end(), dep);
                cycle.assign(it, path.end());
                throw cyclic_dependency(cycle);
            }
            if (dep_color == color_kind::white) {
                dfs_cycle_detect(dep, ctx, color, path);
            }
        }
    }

    path.pop_back();
    color[node] = color_kind::black;
}

void check_cyclic_dependencies(const validation_context& ctx) {
    std::unordered_map<std::type_index, color_kind> color;
    for (auto& [type, _] : ctx.last_descriptor) {
        color[type] = color_kind::white;
    }

    std::vector<std::type_index> path;
    for (auto& [type, c] : color) {
        if (c == color_kind::white) {
            dfs_cycle_detect(type, ctx, color, path);
        }
    }
}

// Check lifetime compatibility for ALL descriptors (keyed and non-keyed)
// deps<> resolves non-keyed targets, so look up dep lifetime from last non-keyed descriptor
void check_lifetime_mismatch(const validation_context& ctx) {
    for (auto* desc : ctx.all) {
        for (auto& dep : desc->dependencies) {
            auto dep_it = ctx.last_descriptor.find(dep);
            if (dep_it == ctx.last_descriptor.end()) continue; // Missing dep handled elsewhere

            lifetime_kind consumer_lt = desc->lifetime;
            lifetime_kind dep_lt = dep_it->second->lifetime;

            bool invalid = false;
            if (consumer_lt == lifetime_kind::singleton) {
                if (dep_lt != lifetime_kind::singleton) {
                // Business rule (REQUIREMENTS.md, section A.4):
                // A scoped component is NOT allowed to depend on a transient one.
                // This is stricter than many DI frameworks (which often permit
                // scoped → transient), but is enforced here to guarantee that all
                // dependencies of a scoped component are at least as long-lived
                // as the scope itself.
                    invalid = true;
                }
            } else if (consumer_lt == lifetime_kind::scoped) {
                if (dep_lt == lifetime_kind::transient) {
                    invalid = true;
                }
            }
            // Transient can depend on anything — no check needed

            if (invalid) {
                throw lifetime_mismatch(
                    desc->component_type, to_string(consumer_lt),
                    dep, to_string(dep_lt));
            }
        }
    }
}

} // anonymous namespace

// ---------------------------------------------------------------
// Public validation entry point (called by registry::build)
// ---------------------------------------------------------------

void validate_descriptors(const std::vector<descriptor>& descriptors,
                         const build_options& options) {
    validation_context ctx;
    ctx.build(descriptors);

    // Always check missing dependencies, ambiguous dependencies, and cycles
    check_missing_dependencies(ctx);
    check_ambiguous_dependencies(ctx);
    check_cyclic_dependencies(ctx);

    // Optionally check lifetime/scope validation
    if (options.validate_scopes) {
        check_lifetime_mismatch(ctx);
    }
}

} // namespace librtdi
