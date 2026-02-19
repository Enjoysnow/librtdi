#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>
#include <memory>
#include <stdexcept>

using namespace librtdi;

// ---------------------------------------------------------------
// Test interfaces
// ---------------------------------------------------------------

struct IEdge {
    virtual ~IEdge() = default;
    virtual int Value() const = 0;
};

struct EdgeImpl : IEdge {
    int Value() const override { return 1; }
};

struct ThrowingImpl : IEdge {
    ThrowingImpl() { throw std::runtime_error("construction failed!"); }
    int Value() const override { return -1; }
};

// ---------------------------------------------------------------
// Tests
// ---------------------------------------------------------------

TEST_CASE("EdgeCase: self-registration (interface == impl)", "[edge]") {
    registry registry;
    registry.add_singleton<EdgeImpl, EdgeImpl>();
    auto resolver = registry.build();

    auto svc = resolver->resolve<EdgeImpl>();
    REQUIRE(svc->Value() == 1);
}

TEST_CASE("EdgeCase: constructor throws — exception propagates, no half-constructed state", "[edge]") {
    registry registry;
    registry.add_singleton<IEdge, ThrowingImpl>();
    auto resolver = registry.build();

    REQUIRE_THROWS_AS(
        resolver->resolve<IEdge>(),
        resolution_error);

    // Retrying should also throw (singleton not cached on failure)
    REQUIRE_THROWS_AS(
        resolver->resolve<IEdge>(),
        resolution_error);
}

TEST_CASE("EdgeCase: try_resolve for unregistered returns nullptr", "[edge]") {
    registry registry;
    registry.add_singleton<EdgeImpl, EdgeImpl>();
    auto resolver = registry.build();

    auto svc = resolver->try_resolve<IEdge>();
    REQUIRE(svc == nullptr);
}

TEST_CASE("EdgeCase: descriptors accessible before build", "[edge]") {
    registry registry;
    registry.add_singleton<IEdge, EdgeImpl>();
    registry.add_transient<IEdge, EdgeImpl>();

    REQUIRE(registry.descriptors().size() == 2);
}

TEST_CASE("EdgeCase: resolve_by_index out of range throws", "[edge]") {
    registry registry;
    registry.add_singleton<IEdge, EdgeImpl>();
    auto resolver = registry.build();

    REQUIRE_THROWS_AS(
        resolver->resolve_by_index(999),
        di_error);
}
