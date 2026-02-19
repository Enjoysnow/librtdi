#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>
#include <memory>

using namespace librtdi;

// ---------------------------------------------------------------
// Test interfaces
// ---------------------------------------------------------------

struct IScopedComp {
    virtual ~IScopedComp() = default;
};

struct ScopedImpl : IScopedComp {
    static int alive_count;
    ScopedImpl() { ++alive_count; }
    ~ScopedImpl() override { --alive_count; }
};
int ScopedImpl::alive_count = 0;

// ---------------------------------------------------------------
// Tests
// ---------------------------------------------------------------

TEST_CASE("scope: create_scope returns valid scope", "[scope]") {
    registry registry;
    registry.add_scoped<IScopedComp, ScopedImpl>();
    auto resolver = registry.build();

    auto scope = resolver->create_scope();
    REQUIRE(scope != nullptr);
    auto& r = scope->get_resolver();
    REQUIRE_FALSE(r.is_root());
}

TEST_CASE("scope: scoped instances released when scope destroyed", "[scope]") {
    ScopedImpl::alive_count = 0;
    registry registry;
    registry.add_scoped<IScopedComp, ScopedImpl>();
    auto resolver = registry.build();

    std::weak_ptr<IScopedComp> weak;
    {
        auto scope = resolver->create_scope();
        auto svc = scope->get_resolver().resolve<IScopedComp>();
        weak = svc;
        REQUIRE(ScopedImpl::alive_count == 1);
    }
    REQUIRE(weak.expired());
    REQUIRE(ScopedImpl::alive_count == 0);
}

TEST_CASE("scope: nested scopes are independent", "[scope]") {
    registry registry;
    registry.add_scoped<IScopedComp, ScopedImpl>();
    auto resolver = registry.build();

    auto outer = resolver->create_scope();
    auto inner = outer->get_resolver().create_scope();
    auto a = outer->get_resolver().resolve<IScopedComp>();
    auto b = inner->get_resolver().resolve<IScopedComp>();
    REQUIRE(a.get() != b.get());
}

TEST_CASE("scope: root resolver throws for scoped component", "[scope]") {
    registry registry;
    registry.add_scoped<IScopedComp, ScopedImpl>();
    auto resolver = registry.build();

    REQUIRE_THROWS_AS(
        resolver->resolve<IScopedComp>(),
        no_active_scope);
}

TEST_CASE("scope: root resolver is_root returns true", "[scope]") {
    registry registry;
    auto resolver = registry.build({.validate_on_build = false});
    REQUIRE(resolver->is_root());
}
