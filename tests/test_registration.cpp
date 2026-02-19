#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>
#include <memory>

using namespace librtdi;

// ---------------------------------------------------------------
// Test interfaces and implementations
// ---------------------------------------------------------------

struct ISimple {
    virtual ~ISimple() = default;
    virtual int Value() const = 0;
};

struct SimpleImpl : ISimple {
    int Value() const override { return 42; }
};

struct AnotherImpl : ISimple {
    int Value() const override { return 99; }
};

// ---------------------------------------------------------------
// Tests
// ---------------------------------------------------------------

TEST_CASE("Registration: register and build succeeds", "[registration]") {
    registry registry;
    registry.add_singleton<ISimple, SimpleImpl>();
    auto resolver = registry.build();
    REQUIRE(resolver != nullptr);
}

TEST_CASE("Registration: empty registry build succeeds", "[registration]") {
    registry registry;
    auto resolver = registry.build({.validate_on_build = false});
    REQUIRE(resolver != nullptr);
}

TEST_CASE("Registration: resolve throws for multiple registrations", "[registration]") {
    registry registry;
    registry.add_singleton<ISimple, SimpleImpl>();
    registry.add_singleton<ISimple, AnotherImpl>();
    auto resolver = registry.build();

    REQUIRE_THROWS_AS(resolver->resolve<ISimple>(), ambiguous_component);
}

TEST_CASE("Registration: resolve_any returns last registered", "[registration]") {
    registry registry;
    registry.add_singleton<ISimple, SimpleImpl>();
    registry.add_singleton<ISimple, AnotherImpl>();
    auto resolver = registry.build();

    auto svc = resolver->resolve_any<ISimple>();
    REQUIRE(svc->Value() == 99); // AnotherImpl registered last
}

TEST_CASE("Registration: cannot register after build", "[registration]") {
    registry registry;
    registry.add_singleton<ISimple, SimpleImpl>();
    auto resolver = registry.build();

    auto fn = [&]{ registry.add_singleton<ISimple, AnotherImpl>(); };
    REQUIRE_THROWS_AS(fn(), di_error);
}

TEST_CASE("Registration: cannot build twice", "[registration]") {
    registry registry;
    registry.add_singleton<ISimple, SimpleImpl>();
    auto resolver = registry.build();

    REQUIRE_THROWS_AS(
        registry.build(),
        di_error);
}
