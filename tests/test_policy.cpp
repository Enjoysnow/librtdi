#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>
#include <memory>

using namespace librtdi;

// ---------------------------------------------------------------
// Test interfaces
// ---------------------------------------------------------------

struct IPolicy {
    virtual ~IPolicy() = default;
    virtual std::string Name() const = 0;
};

struct PolicyA : IPolicy {
    std::string Name() const override { return "A"; }
};

struct PolicyB : IPolicy {
    std::string Name() const override { return "B"; }
};

struct PolicyC : IPolicy {
    std::string Name() const override { return "C"; }
};

// ---------------------------------------------------------------
// Multiple (default — existing behaviour)
// ---------------------------------------------------------------

TEST_CASE("Policy: Multiple is unchanged default", "[policy]") {
    registry registry;
    registry.add_singleton<IPolicy, PolicyA>();
    registry.add_singleton<IPolicy, PolicyB>();
    auto resolver = registry.build();

    auto all = resolver->resolve_all<IPolicy>();
    REQUIRE(all.size() == 2);
    REQUIRE(all[0]->Name() == "A");
    REQUIRE(all[1]->Name() == "B");
}

// ---------------------------------------------------------------
// Single
// ---------------------------------------------------------------

TEST_CASE("Policy: Single — first registration succeeds", "[policy]") {
    registry registry;
    registry.add_singleton<IPolicy, PolicyA>(registration_policy::single);
    auto resolver = registry.build();

    auto svc = resolver->resolve<IPolicy>();
    REQUIRE(svc->Name() == "A");
}

TEST_CASE("Policy: Single — duplicate throws", "[policy]") {
    registry registry;
    registry.add_singleton<IPolicy, PolicyA>(registration_policy::single);

    REQUIRE_THROWS_AS(
        (registry.add_singleton<IPolicy, PolicyB>(registration_policy::single)),
        duplicate_registration);
}

TEST_CASE("Policy: Single — different keys don't conflict", "[policy]") {
    registry registry;
    registry.add_singleton<IPolicy, PolicyA>("k1", registration_policy::single);
    // Same interface, different key — should NOT throw
    REQUIRE_NOTHROW(
        registry.add_singleton<IPolicy, PolicyB>("k2", registration_policy::single));

    auto resolver = registry.build();
    REQUIRE(resolver->resolve<IPolicy>("k1")->Name() == "A");
    REQUIRE(resolver->resolve<IPolicy>("k2")->Name() == "B");
}

TEST_CASE("Policy: Single — second call with Multiple still throws because first was Single", "[policy]") {
    // Single locks the (type, key) slot. Subsequent Multiple is rejected.
    registry registry;
    registry.add_singleton<IPolicy, PolicyA>(registration_policy::single);
    // Second call with default Multiple should throw — slot is locked
    REQUIRE_THROWS_AS(
        (registry.add_singleton<IPolicy, PolicyB>()),
        duplicate_registration);
}

TEST_CASE("Policy: Single — Multiple then Single upgrades to locked single slot", "[policy]") {
    // One existing registration can be upgraded to Single lock without
    // adding a second descriptor.
    registry registry;
    registry.add_singleton<IPolicy, PolicyA>(); // Multiple (default)

    REQUIRE_NOTHROW(
        registry.add_singleton<IPolicy, PolicyB>(registration_policy::single));

    // Lock-only upgrade keeps only one descriptor in the slot
    const auto& descs = registry.descriptors();
    std::size_t count = 0;
    const descriptor* slot_desc = nullptr;
    for (auto& d : descs) {
        if (d.component_type == typeid(IPolicy) && d.key.empty()) {
            ++count;
            slot_desc = &d;
        }
    }
    REQUIRE(count == 1);
    REQUIRE(slot_desc != nullptr);
    REQUIRE(slot_desc->is_single_slot);

    // After lock, Multiple should be rejected
    REQUIRE_THROWS_AS(
        (registry.add_singleton<IPolicy, PolicyB>()),
        duplicate_registration);
}

TEST_CASE("Policy: Single — then Replace is allowed", "[policy]") {
    // Replace can override a Single-locked slot.
    registry registry;
    registry.add_singleton<IPolicy, PolicyA>(registration_policy::single);
    REQUIRE_NOTHROW(
        registry.add_singleton<IPolicy, PolicyB>(registration_policy::replace));

    const auto& descs = registry.descriptors();
    const descriptor* slot_desc = nullptr;
    for (auto& d : descs) {
        if (d.component_type == typeid(IPolicy) && d.key.empty()) {
            slot_desc = &d;
            break;
        }
    }
    REQUIRE(slot_desc != nullptr);
    REQUIRE(slot_desc->is_single_slot);

    REQUIRE_THROWS_AS(
        (registry.add_singleton<IPolicy, PolicyA>()),
        duplicate_registration);

    auto resolver = registry.build();
    auto all = resolver->resolve_all<IPolicy>();
    REQUIRE(all.size() == 1);
    REQUIRE(all[0]->Name() == "B");
}

TEST_CASE("Policy: Single — then Skip is allowed (silently skips)", "[policy]") {
    registry registry;
    registry.add_singleton<IPolicy, PolicyA>(registration_policy::single);
    REQUIRE_NOTHROW(
        registry.add_singleton<IPolicy, PolicyB>(registration_policy::skip));
    auto resolver = registry.build();
    auto svc = resolver->resolve<IPolicy>();
    REQUIRE(svc->Name() == "A");
}

TEST_CASE("Policy: Single — then Single throws", "[policy]") {
    // Double Single is still rejected (existing test covers this, but
    // now it also triggers the single_locked pre-check for the second call).
    registry registry;
    registry.add_singleton<IPolicy, PolicyA>(registration_policy::single);
    REQUIRE_THROWS_AS(
        (registry.add_singleton<IPolicy, PolicyB>(registration_policy::single)),
        duplicate_registration);
}

// ---------------------------------------------------------------
// Replace
// ---------------------------------------------------------------

TEST_CASE("Policy: Replace — replaces all previous registrations", "[policy]") {
    registry registry;
    registry.add_singleton<IPolicy, PolicyA>();
    registry.add_singleton<IPolicy, PolicyB>();
    // Replace should remove both A and B, add C
    registry.add_singleton<IPolicy, PolicyC>(registration_policy::replace);
    auto resolver = registry.build();

    auto all = resolver->resolve_all<IPolicy>();
    REQUIRE(all.size() == 1);
    REQUIRE(all[0]->Name() == "C");
}

TEST_CASE("Policy: Replace — resolve_all returns only replacement", "[policy]") {
    registry registry;
    registry.add_transient<IPolicy, PolicyA>();
    registry.add_transient<IPolicy, PolicyB>();
    registry.add_transient<IPolicy, PolicyC>(registration_policy::replace);
    auto resolver = registry.build();

    auto scope = resolver->create_scope();
    auto all = scope->get_resolver().resolve_all<IPolicy>();
    REQUIRE(all.size() == 1);
    REQUIRE(all[0]->Name() == "C");
}

// ---------------------------------------------------------------
// Skip
// ---------------------------------------------------------------

TEST_CASE("Policy: Skip — first registration kept", "[policy]") {
    registry registry;
    registry.add_singleton<IPolicy, PolicyA>();
    registry.add_singleton<IPolicy, PolicyB>(registration_policy::skip);
    auto resolver = registry.build();

    // Should still be A (skip silently ignored B)
    auto svc = resolver->resolve<IPolicy>();
    REQUIRE(svc->Name() == "A");
    // Only one registration
    auto all = resolver->resolve_all<IPolicy>();
    REQUIRE(all.size() == 1);
}

TEST_CASE("Policy: Skip — works with keyed registration", "[policy]") {
    registry registry;
    registry.add_singleton<IPolicy, PolicyA>("mykey");
    registry.add_singleton<IPolicy, PolicyB>("mykey", registration_policy::skip);
    auto resolver = registry.build();

    auto svc = resolver->resolve<IPolicy>("mykey");
    REQUIRE(svc->Name() == "A");
}

TEST_CASE("Policy: Skip — registers if no existing", "[policy]") {
    registry registry;
    // Nothing registered yet — Skip should register
    registry.add_singleton<IPolicy, PolicyB>(registration_policy::skip);
    auto resolver = registry.build();

    auto svc = resolver->resolve<IPolicy>();
    REQUIRE(svc->Name() == "B");
}

// ---------------------------------------------------------------
// Policy with deps<>
// ---------------------------------------------------------------

struct IDep {
    virtual ~IDep() = default;
};
struct DepImpl : IDep {};

struct IWithDep {
    virtual ~IWithDep() = default;
};
struct WithDepImpl : IWithDep {
    std::shared_ptr<IDep> dep_;
    explicit WithDepImpl(std::shared_ptr<IDep> d) : dep_(std::move(d)) {}
};

TEST_CASE("Policy: Single with deps<> works", "[policy]") {
    registry registry;
    registry.add_singleton<IDep, DepImpl>();
    registry.add_singleton<IWithDep, WithDepImpl>(deps<IDep>, registration_policy::single);

    REQUIRE_THROWS_AS(
        (registry.add_singleton<IWithDep, WithDepImpl>(deps<IDep>, registration_policy::single)),
        duplicate_registration);

    auto resolver = registry.build();
    REQUIRE(resolver->resolve<IWithDep>() != nullptr);
}
