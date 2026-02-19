#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>
#include <memory>

using namespace librtdi;

// ---------------------------------------------------------------
// Test interfaces
// ---------------------------------------------------------------

struct ICounter {
    virtual ~ICounter() = default;
    virtual int Id() const = 0;
};

static int g_next_id = 0;

struct CounterImpl : ICounter {
    int id_;
    CounterImpl() : id_(g_next_id++) {}
    int Id() const override { return id_; }
};

// ---------------------------------------------------------------
// Tests
// ---------------------------------------------------------------

TEST_CASE("lifetime_kind: singleton returns same instance", "[lifetime]") {
    g_next_id = 0;
    registry registry;
    registry.add_singleton<ICounter, CounterImpl>();
    auto resolver = registry.build();

    auto a = resolver->resolve<ICounter>();
    auto b = resolver->resolve<ICounter>();
    REQUIRE(a.get() == b.get());
    REQUIRE(a->Id() == b->Id());
}

TEST_CASE("lifetime_kind: transient returns different instances", "[lifetime]") {
    g_next_id = 0;
    registry registry;
    registry.add_transient<ICounter, CounterImpl>();
    auto resolver = registry.build();

    // Transient resolved from scope
    auto scope = resolver->create_scope();
    auto& r = scope->get_resolver();
    auto a = r.resolve<ICounter>();
    auto b = r.resolve<ICounter>();
    REQUIRE(a.get() != b.get());
    REQUIRE(a->Id() != b->Id());
}

TEST_CASE("lifetime_kind: scoped returns same instance within scope", "[lifetime]") {
    g_next_id = 0;
    registry registry;
    registry.add_scoped<ICounter, CounterImpl>();
    auto resolver = registry.build();

    auto scope = resolver->create_scope();
    auto& r = scope->get_resolver();
    auto a = r.resolve<ICounter>();
    auto b = r.resolve<ICounter>();
    REQUIRE(a.get() == b.get());
}

TEST_CASE("lifetime_kind: scoped returns different instances across scopes", "[lifetime]") {
    g_next_id = 0;
    registry registry;
    registry.add_scoped<ICounter, CounterImpl>();
    auto resolver = registry.build();

    auto scope1 = resolver->create_scope();
    auto scope2 = resolver->create_scope();
    auto a = scope1->get_resolver().resolve<ICounter>();
    auto b = scope2->get_resolver().resolve<ICounter>();
    REQUIRE(a.get() != b.get());
}

TEST_CASE("lifetime_kind: singleton same across scopes", "[lifetime]") {
    g_next_id = 0;
    registry registry;
    registry.add_singleton<ICounter, CounterImpl>();
    auto resolver = registry.build();

    auto scope1 = resolver->create_scope();
    auto scope2 = resolver->create_scope();
    auto a = scope1->get_resolver().resolve<ICounter>();
    auto b = scope2->get_resolver().resolve<ICounter>();
    auto c = resolver->resolve<ICounter>();
    REQUIRE(a.get() == b.get());
    REQUIRE(a.get() == c.get());
}
