# Singleton Destruction Order Design

Date: 2026-06-24
Project: librtdi
Status: Approved design

## Problem

`resolver` currently stores created singleton instances in `std::unordered_map<std::size_t, erased_ptr>`. When a `resolver` is destroyed, the map destroys its elements in an unspecified order. That order is unrelated to the dependency graph declared through `deps<>`.

This can crash when a singleton consumer keeps a reference to another singleton and touches that dependency during its destructor. If the dependency is destroyed first, the consumer observes a dangling reference. The reported crash has this shape: a service destructor calls a dependency virtual method during process exit, but the dependency object has already been destroyed and the vtable pointer is invalid.

This is a container lifetime bug, not a test framework failure. The tests may pass before the crash because the failure happens during static or process-exit cleanup.

## Goals

- Keep the public API unchanged.
- Guarantee deterministic singleton destruction for valid, acyclic singleton dependency graphs.
- Destroy singleton consumers before the singleton dependencies they declared with `deps<>`.
- Cover all current singleton shapes: single-instance, collection, keyed, forward, and decorated registrations.
- Preserve existing `erased_ptr`, forward, decorator, eager singleton, and lazy singleton semantics.
- Avoid throwing from resolver destruction.
- Add focused regression tests, including sanitizer-oriented validation.

## Non-Goals

- No new public `build_options` field.
- No new public exception type or diagnostic API.
- No attempt to make resolver destruction safe while other threads still use the last `shared_ptr<resolver>`.
- No change to the existing rule that `deps<>` dependencies are non-keyed.
- No change to transient instance ownership.

## Recommended Approach

Use dependency-aware singleton teardown inside `resolver::impl`.

The existing map can remain the lookup cache, but it must no longer be responsible for choosing destruction order. `resolver::impl` should explicitly reset cached singleton entries before the map naturally destroys them.

For a valid graph, the destruction order is the reverse of dependency lifetime: if descriptor `A` depends on descriptor `B`, then `A` must be reset before `B`.

For invalid or unverifiable graphs, use reverse creation order as a deterministic best-effort fallback. This covers configurations where users disabled validation or cycle detection.

## Internal Design

### State

Add an internal creation-order list to `resolver::impl`:

```cpp
std::vector<std::size_t> creation_order;
```

When `resolve_singleton_by_index(idx)` successfully creates and caches a new singleton entry, append `idx` to `creation_order`. If the singleton already exists, do not append it again.

This list serves two purposes:

- only actually-created singleton entries participate in destruction;
- reverse creation order is the fallback when graph ordering cannot cover all entries.

### Ownership

The owner of a singleton remains the cached `erased_ptr` for its descriptor index.

Forward singleton entries may be non-owning (`deleter == nullptr`). They can still be reset safely, but resetting them does not destroy the forwarded target. Decorated singleton descriptors own their outer decorator object; their `decorated_ptr` may contain a non-owning handle to the inner singleton.

The teardown algorithm should reset every created cache entry at most once. Owning entries release objects; non-owning entries become empty no-ops.

### Dependency Edges

Build edges only among created singleton descriptor indices.

For each created singleton descriptor `consumer`, inspect `consumer.dependencies`:

- only dependencies that resolve to singleton slots participate in singleton teardown ordering;
- `dep.is_transient == true` is ignored for teardown ordering because transient objects are not held in the singleton cache;
- `collection<T>` dependencies create edges from `consumer` to every created singleton collection descriptor for `T`;
- bare `T` and `singleton<T>` dependencies create an edge from `consumer` to the created singleton single-instance descriptor for `T`;
- dependency lookups use the existing non-keyed `slot_to_indices` rule, matching current `deps<>` behavior.

The edge direction is:

```text
consumer -> dependency
```

The destruction sequence must place the consumer before the dependency.

### Ordering

Compute a destruction order from the created singleton subgraph. A DFS postorder or Kahn-style topological pass is acceptable as long as it preserves this rule:

```text
if A depends on B, reset A before resetting B
```

When the graph is acyclic, every reachable created singleton should be included in the dependency-aware order.

When the algorithm detects a cycle, missing slot mapping, or any other inconsistency, it must not throw. It should order as much as it can, then append remaining entries in reverse `creation_order`.

### Teardown

Implement controlled teardown in `resolver::impl` destruction:

1. Lock `singleton_mutex`.
2. Compute the dependency-aware destruction order for currently cached entries.
3. Reset entries in that order.
4. Reset any remaining entries in reverse `creation_order`.
5. Clear `singletons` and `creation_order`.

After this, `singletons` natural destruction has no live ownership left to release, so `unordered_map` iteration order no longer affects object lifetime.

The destructor path must not call public resolver APIs, because those APIs can throw and add unrelated diagnostics. It should work directly from `descriptors`, `slot_to_indices`, `singletons`, and `creation_order`.

## Data Flow

### Eager Singletons

`registry::build()` already records singleton descriptor indices and calls `resolve_singleton_by_index()` when `eager_singletons == true`. The new creation-order tracking naturally records the actual eager creation sequence. Dependencies created recursively during eager construction are recorded when they are created.

### Lazy Singletons

Lazy mode records only singleton descriptors that were actually resolved. Uncreated descriptors do not participate in teardown and have no object to destroy.

### Forward Singletons

A forward singleton factory resolves the target singleton, casts it, and returns a non-owning `erased_ptr`. Both the forward descriptor and the target descriptor may appear in the cache if resolved. The forward cache entry should reset before the target when a dependency edge requires it; if no edge exists, reverse creation order is a safe fallback because the non-owning reset does not delete the target.

### Decorated Singletons

A decorated singleton descriptor owns the outer decorator object. Its `decorated_ptr` points at the inner singleton and is often non-owning for singleton inners. The dependency-aware order must destroy decorators before the singleton objects they wrap or depend on.

## Error Handling

Resolver destruction must not throw.

- Topology failures fall back to reverse creation order.
- Missing dependency slots during teardown are ignored.
- Uncreated dependencies are ignored.
- `erased_ptr::reset()` is already `noexcept`; user destructors are still expected not to throw.

No new public exception or diagnostic behavior is introduced.

## Threading

The resolver remains safe for concurrent resolution while it is alive. Teardown should lock `singleton_mutex` to keep internal structures consistent during destruction.

This does not change the user-side lifetime requirement: no thread may continue using a resolver after the last `shared_ptr<resolver>` begins destruction.

## Testing Plan

Add focused regression coverage for deterministic singleton teardown:

- A minimal singleton consumer depends on a singleton dependency and calls the dependency during its destructor. Destroying the resolver must not crash, and the observed event order must be consumer destructor before dependency destructor.
- Eager mode reproduces the reported process-exit shape.
- Lazy mode proves only created singleton entries are reset.
- A chain `A -> B -> C` tears down as `A`, then `B`, then `C`.
- A singleton depending on `collection<IPlugin>` tears down before all created singleton collection items.
- Forward singleton and decorated singleton scenarios preserve existing no-double-free behavior and add event-order assertions.
- A validation-disabled or cyclic configuration does not rely on `unordered_map` natural destruction; it uses stable fallback behavior and does not double reset entries.

Run focused tests first, then the full suite:

```bash
./build/tests/librtdi_tests "[lifetime]" "[eager]" "[forward][decorator]"
ctest --test-dir build --output-on-failure
```

Sanitizer validation should also be included. If the current build enables `LIBRTDI_ENABLE_SANITIZERS`, run the same focused and full tests under sanitizers. If sanitizers are not enabled in the current build, record that in the implementation notes and still run the normal focused and full test suite.

## Acceptance Criteria

- Public API remains source-compatible.
- Existing tests continue to pass.
- New tests fail against the current unordered-map-driven teardown and pass with controlled teardown.
- Valid singleton dependency graphs destroy consumers before dependencies.
- Forward and decorated singleton behavior remains free of double delete.
- Lazy singleton mode only destroys objects that were actually created.
- No exception is thrown from resolver teardown.